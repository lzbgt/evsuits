CC = g++
CFLAGS = -g -Wall -std=c++11

cvsrc = prog1.cpp
cvprog = cvprog

rtspsrc=rtsp.cpp
rtspprog=rtspprog

OPENCV = `pkg-config opencv --cflags --libs`
FFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`


$(cvprog):$(cvsrc)
	$(CC) $(CFLAGS) -o $(cvprog) $(cvsrc) $(OPENCV)

$(rtspprog):$(rtspsrc)
	$(CC) $(CFLAGS) -o $(rtspprog) $(rtspsrc) $(FFMPEG)