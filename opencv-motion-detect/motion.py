#!/bin/python
import cv2, imutils
import os, sys, datetime
import time


class MotionDetector:
    def __init__(self, env):
        self.env = env
        self.cap = cv2.VideoCapture(env['STREAM_ADDR'])
        self.height = self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
        self.width = self.cap.get(cv2.CAP_PROP_FRAME_WIDTH)
        self.fps = self.cap.get(cv2.CAP_PROP_FPS)
        self.avg = None
        print(self.width, self.height, self.fps)
    def run(self):
        while self.cap.isOpened() and cv2.waitKey(1) < 0:
            start = datetime.datetime.now().timestamp()
            ret, frame = self.cap.read()
            if not ret:
                break
            frame = imutils.resize(frame, width=500)
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            gray = cv2.GaussianBlur(gray, (21, 21), 0)
            if self.avg is None:
                self.avg = gray.copy().astype("float")
                continue

            cv2.accumulateWeighted(gray, self.avg, 0.5)
            frameDelta = cv2.absdiff(gray, cv2.convertScaleAbs(self.avg))
            # threshold the delta image, dilate the thresholded image to fill
            # in holes, then find contours on thresholded image
            thresh = cv2.threshold(frameDelta, self.env['SENSITIVITY'], 255, cv2.THRESH_BINARY)[1]
            thresh = cv2.dilate(thresh, None, iterations=2)
            cnts = cv2.findContours(thresh.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            cnts = imutils.grab_contours(cnts)

            text = 'No Motion'
            # loop over the contours
            for c in cnts:
                # if the contour is too small, ignore it
                if cv2.contourArea(c) < self.env['AREA_SIZE']:
                    continue
                # compute the bounding box for the contour, draw it on the frame,
                # and update the text
                (x, y, w, h) = cv2.boundingRect(c)
                cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
                text = "Motion Detected"
        
            # draw the text and timestamp on the frame
            ts = datetime.datetime.now().strftime("%A %d %B %Y %I:%M:%S%p")
            cv2.putText(frame, "Room Status: {}".format(text), (10, 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 2)
            cv2.putText(frame, ts, (10, frame.shape[0] - 10), cv2.FONT_HERSHEY_SIMPLEX,
                0.35, (0, 0, 255), 1)
            cv2.imshow('frame', frame)

            # fps implication
            delta = 1.0/self.fps - (datetime.datetime.now().timestamp() - start)
            delta = 0 if delta < 0 else delta
            time.sleep(delta)

        self.cap.release()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    env = {}
    env['STREAM_ADDR'] = os.getenv('STREAM_ADDR', 'rtsp://172.31.0.121/live/0/sub')
    env['PROC_FPS'] = int(os.getenv('PROC_FPS', 10))
    env['SENSITIVITY'] = int(os.getenv('SENSITIVITY', 20))
    env['AREA_SIZE'] = int(os.getenv('AREA_SIZE', 200))
    detector = MotionDetector(env)
    detector.run()