CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=g++1z
CFLAGS = -g -Wall

LIBOPENCV = `pkg-config opencv --cflags --libs`
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`

.PHONY: all
all: libzmq rtspr

.PHONY: libzmq
libzmq:
	cd vendor/libzmq && [ -f $(CURDIR)/vendor/lib/pkgconfig/libzmq.pc ] || ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor
	cd vendor/libzmq && make -j 4 && make install
rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc)
	
rtsp: rtsp.cpp
	$(CPP) $(CFLAGS) -o rtsp rtsp.cpp $(LIBFFMPEG)

cvprog: prog1.cpp
	$(CPP) $(CPPFLAGS) -o $(cvprog) $(cvsrc) $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

clean:
	rm -fr rtsp cvprog mux rtspr *.dSYM