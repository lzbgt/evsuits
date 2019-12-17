CC = g++
CFLAGS = -g -Wall -std=c++2a
SRCS = main.origin.cpp
PROG = main
SRCS2 = main.cpp
PROG2 = yolo

OPENCV = `pkg-config opencv4 --cflags --libs`
LIBS = $(OPENCV)

all: $(PROG) $(PROG2)

$(PROG):$(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

$(PROG2):$(SRCS2)
	$(CC) $(CFLAGS) -o $(PROG2) $(SRCS2) $(LIBS) -I../opencv-motion-detect/inc -I../opencv-motion-detect/vendor/include