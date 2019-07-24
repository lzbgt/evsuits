CC = g++
CFLAGS = -g -Wall -std=c++11


src1 = prog1.cpp
prog1 = prog1

OPENCV = `pkg-config opencv --cflags --libs`
LIBS = $(OPENCV)

$(prog1):$(src1)
	$(CC) $(CFLAGS) -o $(prog1) $(src1) $(LIBS)