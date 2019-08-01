#!/bin/python
__author__ = 'Bruce.Lu'
__date__ = '2019/07/30'
__enhencements__ = {"1": "state machine based algorithm", "2": "using avg frame instead of previous frame to calc delta",
    "3": "does not depend on accurate process step time", "4": "event notification mechanism"}
__credits__ = ['Zhao LiPeng for the basic frame-delta algorithm', 'Ge.Xu for advices']

import os, sys, datetime, time, cv2, imutils, json, atexit, traceback, re
from threading import Thread
from collections import deque

class FrameFetcher(Thread):
    def init(self):
        self.videoProto = 'RTSP' if 'rtsp' in self.env['VIDEO_ADDR'] else 'FILE'
        if self.videoProto == 'RTSP':
            self.cap = cv2.VideoCapture(env['VIDEO_ADDR'])
            self.height = self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
            self.width = self.cap.get(cv2.CAP_PROP_FRAME_WIDTH)
            self.fps = self.cap.get(cv2.CAP_PROP_FPS)
            self.frameCnt = 0
            self.failedPutCnt = 0
            self.frameSeq = 0
            print(self.width, self.height, self.fps)
        else:
            self.cap = None

    def __init__(self, env, frameHolder):
        Thread.__init__(self)
        self.env = env
        self.frameHolder = frameHolder
        self.init()
        atexit.register(self.cleanup)

    def cleanup(self):
        self.cap.release()

    def run(self):
        if self.videoProto == 'RTSP':
            while True:
                while self.cap.isOpened():
                    start = datetime.datetime.now().timestamp()
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
                            self.frameCnt += 1
                    else:
                        print("error read frame")
                        break

                    delta = datetime.datetime.now().timestamp() - start
                    delta = 1.0 / self.fps - delta
                    if delta > 0:
                        time.sleep(delta)
                        time.sleep(1.0 / self.fps)
                print("error: cap is not opened, reconnecting...")
                self.init()
        else:
            # it's folder, process video slices in double speed
            currentFile = None
            while True:
                try:
                    videos = [{'f': '{}/{}'.format(self.env['VIDEO_ADDR'],f), 't': os.stat(self.env['VIDEO_ADDR'] + '/' + f).st_mtime} for f in os.listdir(self.env['VIDEO_ADDR']) if f.startswith(self.env['SLICE_BASENAME'])]
                    videos = sorted(videos, key=lambda x: x['t'])
                    idx = 0
                    if currentFile:
                        for k,v in enumerate(videos):
                            if v['f'] == currentFile['f'] and v['t'] == currentFile['t']:
                                idx = k
                    if len(videos) < 2 or (currentFile and (idx == len(videos) - 2)):
                        print('waiting for next video ...')
                    else:
                        for f in videos[idx:-1]:
                            if self.cap != None:
                                self.cap.release()
                            print('openning {}'.format(f))
                            self.cap = cv2.VideoCapture(f['f'])
                            if not self.cap.isOpened():
                                print('failed to open file: {}'.format(f))
                                continue
                            if not currentFile:
                                self.height = self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
                                self.width = self.cap.get(cv2.CAP_PROP_FRAME_WIDTH)
                                self.fps = self.cap.get(cv2.CAP_PROP_FPS)
                                self.fps = self.fps * 2
                                self.frameCnt = 0
                                self.failedPutCnt = 0
                                print(self.width, self.height, self.fps)
                            
                            currentFile = f
                            while True:
                                try:
                                    ret, frame = self.cap.read()
                                    if ret:
                                        self.frameHolder.append(frame)
                                        self.frameSeq += self.cap.get(cv2.CAP_PROP_POS_FRAMES)
                                        # calc frame time

                                        self.frameCnt += 1
                                        if self.frameCnt % (self.fps * 2) == 0:
                                            print("frameCnt:{}, file: {}, fseq: {}".format(self.frameCnt, f['f'], ))
                                    else:
                                        break
                                except:
                                    traceback.print_exc()
                                    print("error read frame")
                                    self.cap.release()
                                    self.cap = None
                                    break  
                                finally:
                                    time.sleep(1.0 / self.fps)
                except:
                    traceback.print_exc()
                finally:
                    time.sleep(2)
                

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
                            evtQue.append({'type': 'start', 'ts': int(lastEventEnterTs)})
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
                        if start - lastEventEnterTs > env['EVT_END_SECS']/2:
                            # 'POST' -> 'NONE's
                            eventState = None
                            # emmit event
                            evtQue.append({'type': 'end', 'ts': int(lastEventEnterTs + env['EVT_END_SECS']/2)})                     
            except IndexError:
                pass
            except:
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
                print("event: ", json.dumps(evt))
            except IndexError:
                pass
            except:
                #print("no event")
                pass
            finally:
                time.sleep(4)

if __name__ == '__main__':
    env = {}
    env['VIDEO_ADDR'] = os.getenv('VIDEO_ADDR', '/tmp/test')#'rtsp://172.31.0.121/live/0/sub')
    env['PROC_FPS'] = int(os.getenv('PROC_FPS', 10))

    sensitivity_base = 20
    env['SENSITIVITY_FACT'] = float(os.getenv('SENSITIVITY', 2))
    env['SENSITIVITY'] = int(sensitivity_base * env['SENSITIVITY_FACT'])

    area_base = 20/(500*500)
    env['AREA_FACT'] = float(os.getenv('AREA_FACT', 3)) 
    env['AREA_SIZE'] = int(500 * 500 * env['AREA_FACT']  * area_base)
    
    env['SLICE_NUM'] = int(os.getenv('SLICE_NUM', 6))
    if env['SLICE_NUM'] < 6:
        print('SLICE_NUM must not less than 6, reset it to 6')
        env['SLICE_NUM'] = 6

    env['SLICE_DURATION'] = int(os.getenv('SLICE_DURATION', 20))
    env['SLICE_BASENAME'] = os.getenv('SLICE_BASENAME', 'capture-')
    env['EVT_START_SECS'] = int(os.getenv('EVT_START_SECS', 2))
    env['EVT_END_SECS'] = int(os.getenv('EVT_END_SECS', 30))
    
    evtQue = deque(maxlen=100)
    
    detector = MotionDetector(env, evtQue)
    detector.start()

    consumer = EventConsumer(evtQue)
    consumer.start()

    detector.join()
    consumer.join()
