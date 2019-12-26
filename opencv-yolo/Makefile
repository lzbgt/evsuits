

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	LIBS= -lopencv_dnn -lopencv_core -lopencv_imgproc -lopencv_video -lopencv_videoio -lopencv_imgcodecs
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

SHARED=-I../opencv-motion-detect/inc -I../opencv-motion-detect/vendor/include/opencv4 -L../opencv-motion-detect/vendor/lib



CC = g++
CFLAGS = -g -Wall -std=c++2a
SRCS = main.origin.cpp
PROG = main
SRCS2 = main.cpp
PROG2 = detector

all: $(PROG) $(PROG2)

$(PROG):$(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS)  $(SHARED) $(LIBS) -lopencv_highgui

$(PROG2):$(SRCS2) yolo.hpp
	$(CC) $(CFLAGS) -o $(PROG2) $(SRCS2)  $(SHARED) $(LIBS)

clean:
	rm -fr main detector