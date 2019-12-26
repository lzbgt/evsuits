

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	LIBS= -lopencv_dnn -lopencv_core -lopencv_imgproc -lopencv_video
endif
ifeq ($(UNAME_S),Darwin)
	OPENCV = `pkg-config opencv4 --cflags --libs`
	LIBS = $(OPENCV)
endif
UNAME_P := $(shell uname -p)
ifeq ($(UNAME_P),x86_64)
	NOP=
endif
ifneq ($(filter %86,$(UNAME_P)),)
	NOP=
endif
ifneq ($(filter arm%,$(UNAME_P)),)
	NOP=
endif



CC = g++
CFLAGS = -g -Wall -std=c++2a
SRCS = main.origin.cpp
PROG = main
SRCS2 = main.cpp
PROG2 = yolo

all: $(PROG) $(PROG2)

$(PROG):$(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

$(PROG2):$(SRCS2) yolo.hpp
	$(CC) $(CFLAGS) -o $(PROG2) $(SRCS2) $(LIBS) -I../opencv-motion-detect/inc -I../opencv-motion-detect/vendor/include -L../opencv-motion-detect/vendor/lib

clean:
	rm -fr main yolo