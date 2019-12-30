import paho.mqtt.client as mqtt
import time, json

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
    payload = str(msg.payload)
    print(msg.topic+" "+ payload)
    if userdata:
      print(userdata, payload)
      userdata(json.loads(payload))

  @staticmethod
  def on_disconnect(client, userdata, rc):
    #topic = "video.ai/v1.0/task"
    #client.publish(topic, payload=None, qos=1, retian=False)
    print("disconnected")
  def __init__(self, callback, host = 'evcloud.ilabservice.cloud', port = 1883):
    '''
    Parameters
    '''
    self.client = mqtt.Client("vamqtt",userdata=callback) #, protocol=mqtt.MQTTv5)
    self.client.on_connect = VAMMQTTClient.on_connect
    self.client.on_message = VAMMQTTClient.on_message

    self.client.connect_async(host, port, 30)
    self.client.loop_start()

if __name__ == "__main__":
  mq = VAMMQTTClient(None)
  while True:
    time.sleep(1)