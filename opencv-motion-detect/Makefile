CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=gnu++1z
CFLAGS = -g -Wall

LIBOPENCV = `pkg-config opencv --cflags --libs`
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec --cflags --libs`
LIBS=-lpthread
SQLITE=vendor/sqlite/sqlite3.c
HEADERS=-Iinc

.PHONY: libzmq
all: evmgr evpuller evpusher

.PHONY: libzmq
libzmq:
	cd vendor/libzmq && [ ! -f $(CURDIR)/vendor/lib/pkgconfig/libzmq.pc ] || ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor
	cd vendor/libzmq && make -j 4 && make install

# sqlite C object
sqlite3.o: vendor/sqlite/sqlite3.c
	gcc -D SQLITE_THREADSAFE=1 -c vendor/sqlite/sqlite3.c

evmgr: evmgr.cpp database.cpp inc/common.hpp inc/tinythread.hpp sqlite3.o
	$(CPP) $(CPPFLAGS) -o evmgr evmgr.cpp sqlite3.o database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpuller: evpuller.cpp database.cpp inc/common.hpp inc/tinythread.hpp sqlite3.o
	$(CPP) $(CPPFLAGS) -o evpuller evpuller.cpp sqlite3.o database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpusher: evpusher.cpp inc/common.hpp inc/tinythread.hpp database.cpp sqlite3.o
	$(CPP) $(CPPFLAGS) -o evpusher evpusher.cpp database.cpp sqlite3.o $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(LIBS)

cvsample: cvsample.cpp
	$(CPP) $(CPPFLAGS) -o cvsample cvsample.cpp $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

.PHONY: clean
clean:
	rm -fr evmgr evpuller evpusher *.dSYM *.out *.o