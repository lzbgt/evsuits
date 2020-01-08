from flask import Flask, escape, request, jsonify, g, url_for, current_app
import paho.mqtt.client as mqtt
from cerberus import schema_registry, Validator
from celery import Celery
from azure.storage.fileshare import ShareFileClient
import random
import os, yaml, logging, time, datetime, threading, json, subprocess, shlex, re, string
import pdb,traceback, sys

random.seed(datetime.datetime.now())


"""
va task schema:
  {
    "cameraId": "D72154040",
    "endTime": 1577267418999,
    "image": "http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/firstFrame.jpg",
    "length": 260,
    "startTime": 1550143347000,
    "video": "http://evcloudsvc.ilabservice.cloud/video/D72154040/1550143347000-1577267418999/1550143347000-1577267418999.mp4"
  }

"""
VA_SCHEMAS = {
  'task': {
    'cameraId': {'type': 'string', 'dependencies': ['startTime', 'endTime']},
    'startTime': {'type': 'integer', 'dependencies':'cameraId'},
    'endTime': {'type': 'integer', 'dependencies':'cameraId'},
    'length': {'type': 'integer'},
    'image': {'type': 'string'},
    'video': {'type': 'string'}
  },
}

aan = os.getenv('AAN', 'ilsvideostablediag')
aak = os.getenv('AAK', 'rWeA/cUiWAsDqGHO0lfDB5eDHNZxCChrH0pMvICdNJR6tt+hE2tHlSl9kUEjqyOY6cztPWaaRbbeoI47uNEeWA==')
CONNSTR="DefaultEndpointsProtocol=https;AccountName={};AccountKey={};EndpointSuffix=core.chinacloudapi.cn".format(aan,aak)
#CONNSTR1= 'DefaultEndpointsProtocol=https;AccountName=ilsvideostablediag;AccountKey=rWeA/cUiWAsDqGHO0lfDB5eDHNZxCChrH0pMvICdNJR6tt+hE2tHlSl9kUEjqyOY6cztPWaaRbbeoI47uNEeWA==;EndpointSuffix=core.chinacloudapi.cn'
#if CONNSTR1 != CONNSTR:
#  print("======\n\n======= no valid key\n{}\n{}".format(CONNSTR1, CONNSTR))
SHARENAME=os.getenv('SHARE','pre-data')

MQTT_HOST=os.getenv('MQTT_HOST','evcloud.ilabservice.cloud')
MQTT_PORT=int(os.getenv('MQTT_PORT', 1883))
REDIS_ADDR = os.getenv('REDIS', 'redis://localhost:6379')
workd = os.getenv('BIN_DIR', '../')
binName = os.getenv('BIN_NAME', 'detector ')
binPrefix = os.getenv('BIN_PRE', '')
configDir = os.getenv('CFG_DIR', workd)

print("CONFIG: \n\tMQTT: {}:{}\n\tBIN_NAME: {}".format(MQTT_HOST, MQTT_PORT, binName))

def downloadFile(ipcSn, dirName, fileName, destDir):
  file_path=ipcSn + '/'+dirName+'/'+fileName
  destDir = destDir + '/' + fileName
  print("downloading {}: {} {} {}".format(destDir, ipcSn, dirName, file_path))
  with ShareFileClient.from_connection_string(conn_str=CONNSTR, share_name=SHARENAME, file_path=file_path) as fc:
      with open(destDir, "wb") as f:
          data = fc.download_file()
          data.readinto(f)

def uploadFile(ipcSn, dirName, fileName, srcPath):
  file_path=ipcSn + '/'+dirName+'/' + fileName
  fc = ShareFileClient.from_connection_string(conn_str=CONNSTR, share_name=SHARENAME, file_path=file_path)
  with open(srcPath + '/' + fileName, "rb") as source_file:
    fc.upload_file(source_file)

class VAMMQTTClient:
  # The callback for when the client receives a CONNACK response from the server.
  @staticmethod
  def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    topic = '$queue/video.ai/v1.0/task'
    client.subscribe(topic, qos=1)
    print('subscribed to ', topic)
  #client.subscribe("$queue/video.ai/v1.0/task", qos=1)

  # The callback for when a PUBLISH message is received from the server.
  @staticmethod
  def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8')
    print(msg.topic+" "+ payload)
    if userdata:
      try:
        jd = json.loads(payload)
        userdata(jd)
      except Exception as e:
        print('exception in process message:', e)
        #extype, value, tb = sys.exc_info()
        #traceback.print_exc()
        #pdb.post_mortem(tb)

  @staticmethod
  def on_disconnect(client, userdata, rc):
    #topic = "video.ai/v1.0/task"
    #client.publish(topic, payload=None, qos=1, retian=False)
    print("disconnected")
  def __init__(self, callback, host = MQTT_HOST, port = MQTT_PORT):
    '''
    Parameters
    '''
    self.client = mqtt.Client("vamqtt",userdata=callback) #, protocol=mqtt.MQTTv5)
    self.client.on_connect = VAMMQTTClient.on_connect
    self.client.on_message = VAMMQTTClient.on_message
    self.client.connect_async(host, port, 30)
    self.client.loop_start()

app = Flask(__name__,
  static_url_path='', 
  static_folder='web/main/dist')
logger = app.logger

app.config['broker_url'] = REDIS_ADDR
app.config['result_backend'] = REDIS_ADDR
worker = Celery(app.name, broker=app.config['broker_url'])
worker.conf.update(app.config)
worker.conf.update(
  task_serializer='json',
  #accept_content=['json'],
  result_serializer='json',
  #timezone='Europe/Oslo',
  enable_utc=True)

def take_task(task):
  ret = {'code': 0,'msg': 'ok'}
  print("taking task", json.dumps(task))
  taskValidator = Validator(VA_SCHEMAS['task'], allow_unknown=True)
  if not taskValidator.validate(task):
    ret['code'] = 1
    ret['msg'] = 'invalid request body'
    ret['data'] = taskValidator.errors
  else:
    # process
    video_analysis.apply_async(args=[task])
  print(json.dumps(ret))
  return ret

@app.route('/api/video.ai/v1.0/task', methods=['POST'])
def new_task():
  ret = take_task(request.json)
  return jsonify(ret);

@worker.task
def video_analysis(data):
  ret = {'code': 0, 'msg': 'ok'}
  ret['target'] = data
  print(json.dumps(data))
  try:
    if 'cameraId' in data: # azure storage
      # get azure storage video
      ipcSN = data["cameraId"]
      dirName = "{}-{}".format(data["startTime"],data["endTime"])
      fileName = dirName + '.mp4'
      strRand = "{}-{}".format(data["startTime"], ''.join(random.choice(string.ascii_letters) for i in range(6)))
      downloadDir = "{}/{}/".format(os.getenv('DL_DIR', workd) + '/' + ipcSN, strRand)
      os.system('mkdir -p ' + downloadDir)
      downloadFile(ipcSN, dirName, fileName, downloadDir)
      print("downloaded file {} into {}".format(fileName, downloadDir))
      # analyze
      #cmdLine = '/Users/blu/work/opencv-projects/opencv-yolo/detector /Users/blu/work/opencv-projects/opencv-yolo/web/1550143347000-1577267418999.mp4 -c /Users/blu/work/opencv-projects/opencv-yolo/'
      prefix = binPrefix + ' ' if binPrefix else binPrefix
      cmdLine = prefix + workd + '/' + binName + ' ' +  downloadDir + fileName + ' -c ' + configDir + ' -o ' + downloadDir + '/detect.jpg'
      cmdArgs = shlex.split(cmdLine)
      print(cmdLine, '\n\n', cmdArgs)
      output = subprocess.check_output(cmdArgs)
      print(output)
      # parse
      for line in output.decode('utf-8').split('\n'):
        print("\n", line)
        m = re.match(r".*? found (\w+) ([\d\.]+) .*? image: .*?/([_\w\d]+.jpg)", line)
        ret['data'] = {}
        ret['data']['humanDetect'] = {}
        if m:
          ret['data']['humanDetect']['found'] = 1
          ret['data']['humanDetect']['level'] = m.group(2)
          ret['data']['humanDetect']['image'] = m.group(3)
          print('found {}: {}, img: {}'.format(m.group(1), m.group(2), m.group(3)))
          # write json
          jsonFile = downloadDir + '/' + 'result.json'
          with open(jsonFile, 'w') as outfile:
            json.dump(ret, outfile)
          # upload
          uploadFile(ipcSN, dirName, 'result.json', downloadDir)
          uploadFile(ipcSN, dirName,  m.group(3), downloadDir)
          mc = mqtt.Client("vamqtt-pub")
          mc.connect(MQTT_HOST, MQTT_PORT)
          mc.publish('video.ai/v1.0/result', json.dumps(ret), qos=1)
          break
        else:
          ret['data']['humanDetect']['found'] = 0
    elif 'video' in data: # http
      ret['code'] = 2
      ret['msg'] = 'not impelemented yet for http'
    else: # no video
      ret['code'] = 1
      ret['msg'] = 'no video specified'
      return ret
  except Exception as e:
    print("exception in va worker: {}".format(e));
    ret['code'] = -1
    ret['msg'] = str(e)
    #extype, value, tb = sys.exc_info()
    #traceback.print_exc()
  try:
    os.system('rm -fr ' + downloadDir)
  except Exception as e:
    print('cascaded exception in va: {}'.format(e))
    #raise Exception()
  # pub msg
  return ret

if __name__ == '__main__':
  mq = VAMMQTTClient(take_task)
  app.config['mc'] = mq.client
  app.run(host='0.0.0.0', port = '5000')


