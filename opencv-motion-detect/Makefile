CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=gnu++1z
CFLAGS = -g -Wall

cvsrc = prog1.cpp
cvprog = cvprog

OPENCV = `pkg-config opencv --cflags --libs`
FFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`


all:cvprog mux rtsp rtspr

rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) -o rtspr rtsp-relay.cpp $(FFMPEG)
	
rtsp: rtsp.cpp
	$(CPP) $(CFLAGS) -o rtsp rtsp.cpp $(FFMPEG)

cvprog:prog1.cpp
	$(CPP) $(CPPFLAGS) -o $(cvprog) $(cvsrc) $(OPENCV)

mux:demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(FFMPEG)

clean:
	rm -fr rtsp cvprog mux rtspr