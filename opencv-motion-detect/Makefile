CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=gnu++1z
CFLAGS = -g -Wall

LIBOPENCV = `pkg-config opencv --cflags --libs`
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`
LIBS=-lpthread

.PHONY: all
all: libzmq evmgr evpusher rtspr

.PHONY: libzmq
libzmq:
	cd vendor/libzmq && [ ! -f $(CURDIR)/vendor/lib/pkgconfig/libzmq.pc ] || ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor
	cd vendor/libzmq && make -j 4 && make install
evmgr: evmgr.cpp inc/common.hpp inc/tinythread.hpp
	$(CPP) $(CPPFLAGS) -o evmgr evmgr.cpp $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpusher: evpusher.cpp inc/common.hpp inc/tinythread.hpp
	$(CPP) $(CPPFLAGS) -o evpusher evpusher.cpp $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)


rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(LIBS)

cvprog: prog1.cpp
	$(CPP) $(CPPFLAGS) -o $(cvprog) $(cvsrc) $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

clean:
	mkdir -p vendor/lib/pkgconfig/bak
	mv vendor/lib/pkgconfig/*.pc vendor/lib/pkgconfig/bak
	rm -fr rtspr evmgr evpusher *.dSYM vendor/lib/pkgconfig/*.pc
	#cd vendor/libzmq && make clean