CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=c++11
CFLAGS = -g -Wall

cvsrc = prog1.cpp
cvprog = cvprog


OPENCV = `pkg-config opencv --cflags --libs`
FFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`


all:cvprog mux rtsp

rtsp: rtsp.c
	$(CC) $(CFLAGS) -o rtsp rtsp.c $(FFMPEG)

cvprog:prog1.cpp
	$(CPP) $(CPPFLAGS) -o $(cvprog) $(cvsrc) $(OPENCV)

mux:demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(FFMPEG)

clean:
	rm -fr rtsp cvprog mux