#!/bin/python
__author__ = 'Bruce.Lu'
__date__ = '2019/07/30'
__enhencements__ = {"1": "state machine based algorithm", "2": "using avg frame instead of previous frame to calc delta",
    "3": "does not depend on accurate process step time", "4": "event notification machanism"}
__credits__ = 'Zhao LiPeng for the frame-delta algorithm'


import os, sys, datetime, time, cv2, imutils, json, atexit, traceback
from threading import Thread
from collections import deque

class FrameFetcher(Thread):
    def init(self):
        self.cap = cv2.VideoCapture(env['STREAM_ADDR'])
        self.videoProto = 'RTSP' if 'rtsp' in self.env['STREAM_ADDR'] else 'FILE'
        self.height = self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
        self.width = self.cap.get(cv2.CAP_PROP_FRAME_WIDTH)
        self.fps = self.cap.get(cv2.CAP_PROP_FPS)
        self.frameCnt = 0
        self.failedPutCnt = 0
        print(self.width, self.height, self.fps)

    def __init__(self, env, frameHolder):
        Thread.__init__(self)
        self.env = env
        self.frameHolder = frameHolder
        self.init()
        atexit.register(self.cleanup)

    def cleanup(self):
        self.cap.release()

    def run(self):
        while True:
            while self.cap.isOpened():
                ret, frame = self.cap.read()
                if ret:
                    try:
                        self.frameHolder.append(frame)
                        if self.frameCnt % (self.fps * 2) == 0:
                            print("frameCnt: ", self.frameCnt)
                    except:
                        self.failedPutCnt += 1
                        if self.failedPutCnt % (self.fps * 2) == 0:
                            print("failedPutCnt: ", self.failedPutCnt)
                    finally:
                        self.frameCnt+=1
                else:
                    print("error read frame")
                time.sleep(1.0 / self.fps)
            
            print("error: cap is not opened, reconnecting...")
            self.init()

class MotionDetector(Thread):
    def __init__(self, env, evtQue):
        Thread.__init__(self)
        self.env = env
        self.avg = None
        self.evtQue = evtQue

    def run(self):
        # start image grab thread
        frameHolder = deque(maxlen=1)
        fet = FrameFetcher(self.env, frameHolder)
        fet.start()

        # event state machine
        lastEventEnterTs = None
        eventState = None # pre, in, post, none
        evtQue = self.evtQue
        cntContNoEvent = 0
        cntContEvent = 0
        while True:
            start = datetime.datetime.now().timestamp()
            try:
                frame = frameHolder.popleft()
                gray = cv2.resize(frame.copy(), (500, 500))
                gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
                gray = cv2.GaussianBlur(gray, (21, 21), 0)
                if self.avg is None:
                    self.avg = gray.copy().astype("float")
                    # use first frame as initial image for calculating moving avg.
                    continue
                cv2.accumulateWeighted(gray, self.avg, 0.5)
                frameDelta = cv2.absdiff(gray, cv2.convertScaleAbs(self.avg))
                thresh = cv2.threshold(frameDelta, self.env['SENSITIVITY'], 255, cv2.THRESH_BINARY)[1]
                thresh = cv2.dilate(thresh, None, iterations=2)
                cnts, hierarchy = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
                hasEvent = False
                for c in cnts:
                    # if the contour is too small, ignore it
                    if cv2.contourArea(c) < self.env['AREA_SIZE']:
                        continue
                    else:
                        hasEvent = True
                        break

                # live logging
                if hasEvent:
                    cntContEvent += 1
                    cntContNoEvent = 0
                    if cntContEvent %(self.env['PROC_FPS'] * 2) == 0:
                        print('continous event cnt: {}, current state: {}'.format(cntContEvent, eventState))
                else:
                    cntContNoEvent += 1
                    cntContEvent = 0
                    if cntContNoEvent %(self.env['PROC_FPS'] * 2) == 0:
                        print('continous no event cnt: {},  current state: {}'.format(cntContNoEvent, eventState))
                    
                # statemachine for event
                if eventState == None:
                    if hasEvent:
                        eventState = 'PRE'
                        lastEventEnterTs = start
                    else:
                        pass
                elif eventState == 'PRE':
                    if hasEvent:
                        if start - lastEventEnterTs > env['EVT_START_SECS']:
                            eventState = 'PRE'
                        else:
                            # state transaction: 'PRE' -> 'IN'
                            eventState = 'IN'
                            evtQue.append({'type': 'start', 'ts': int(lastEventEnterTs), 'frame':frame})
                        # update ts
                        lastEventEnterTs = start
                    else:
                        # state transaction: 'PRE' -> 'NONE'
                        if start - lastEventEnterTs > env['EVT_START_SECS']:
                            eventState = None
                        else:
                            pass
                elif eventState == 'IN':
                    if not hasEvent:
                        if start - lastEventEnterTs > env['EVT_END_SECS']/2:
                            # 'IN' -> 'POST'
                            eventState = 'POST'
                    else:
                        lastEventEnterTs = start

                elif eventState == 'POST':
                    if not hasEvent:
                        if start - lastEventEnterTs > env['EVT_END_SECS']:
                            # 'POST' -> 'NONE's
                            eventState = None
                            # emmit event
                            evtQue.append({'type': 'end', 'ts': int(lastEventEnterTs + env['EVT_END_SECS']/2)})  
                    else:
                        eventState = 'IN'
                        lastEventEnterTs = start
                        
            except IndexError:
                pass                   
            except Exception as e:
                traceback.print_exc()
            finally:
                # fps implication
                delta = 1.0/self.env['PROC_FPS'] - (datetime.datetime.now().timestamp() - start)
                delta = 0 if delta < 0 else delta
                time.sleep(delta)

class EventConsumer(Thread):
    def __init__(self, evtQue):
        Thread.__init__(self)
        self.evtQue = evtQue
    
    def run(self):
        while True:
            try:
                evt = self.evtQue.popleft()
                print('event {{type: {}, ts: {}}}'.format(evt['type'], evt['ts']))
                if evt['type'] == 'start':
                    cv2.imwrite(('event-{}.jpg'.format(evt['ts'])), evt['frame'])
            except IndexError:
                pass
            except:
                traceback.print_exc()
            finally:
                time.sleep(4)

if __name__ == '__main__':
    env = {}
    env['STREAM_ADDR'] = os.getenv('STREAM_ADDR', 'rtsp://172.31.0.121/live/0/sub')
    env['PROC_FPS'] = int(os.getenv('PROC_FPS', 10))

    sensitivity_base = 20
    env['SENSITIVITY_FACT'] = float(os.getenv('SENSITIVITY', 2))
    env['SENSITIVITY'] = int(sensitivity_base * env['SENSITIVITY_FACT'])

    area_base = 20/(500*500)
    env['AREA_FACT'] = float(os.getenv('AREA_FACT', 3)) 
    env['AREA_SIZE'] = int(500 * 500 * env['AREA_FACT']  * area_base)
    
    env['SLICE_NUM'] = int(os.getenv('SLICE_NUM', 6))
    env['SLICE_DURATION'] = int(os.getenv('SLICE_DURATION', 20))
    env['EVT_START_SECS'] = int(os.getenv('EVT_START_SECS', 2))
    env['EVT_END_SECS'] = int(os.getenv('EVT_END_SECS', 30))
    env['DEMO_IMG'] = bool(os.getenv('DEMO_IMG', False))
    
    evtQue = deque(maxlen=100)
    
    detector = MotionDetector(env, evtQue)
    detector.start()

    consumer = EventConsumer(evtQue)
    consumer.start()

    detector.join()
    consumer.join()
