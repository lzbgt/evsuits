#
#
#

from flask import Flask, escape, request, jsonify, g, url_for
from cerberus import schema_registry, Validator
from celery import Celery
import os, yaml, logging, time, datetime, threading
from vamqtt import VAMMQTTClient

"""
va task schema:
    {
        "cameraId": "D72158932",
        "endTime": 1576842488000,
        "image": "http://40.73.41.176/video/D72158932/1576842457000-1576842488999/firstFrame.jpg",
        "length": 260,
        "startTime": 1576842457000,
        "video": "http://40.73.41.176/video/D72158932/1576842457000-1576842488999/1576842457000-1576842488999.mp4"
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

app = Flask(__name__,
            static_url_path='', 
            static_folder='web/main/dist')
logger = app.logger

REDIS_ADDR = os.getenv('REDIS', 'redis://localhost:6379')
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

@app.route('/api/video.ai/v1.0/task', methods=['POST'])
def new_task():
    ret = {'code': 0,'msg': 'ok'}
    taskValidator = Validator(VA_SCHEMAS['task'])
    if not taskValidator.validate(request.json):
        ret['code'] = 1
        ret['msg'] = 'invalid request body'
        ret['data'] = taskValidator.errors
        return jsonify(ret)
    else:
        # process
        video_analysis.apply_async(args=[request.json])
        return jsonify(ret)

@worker.task
def video_analysis(data):
    ret = {'code': 0, 'msg': 'ok'}
    # get video
    if 'cameraId' in data: # azure storage
        pass
    elif 'video' in data: # http
        pass
    else: # no video
        ret['code'] = 1
        ret['msg'] = 'no video specified'
        return ret

    # analyze
    logger.info("aaaa")
    return 'aaa'


if __name__ == '__main__':
    mq = VAMMQTTClient(new_task)
    app.run(host='0.0.0.0', port = '5000')


