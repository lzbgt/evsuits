
DEBUG=-g
CC = gcc
CPP = g++
CPPFLAGS = $(DEBUG) -Wall -std=gnu++1z
CFLAGS = $(DEBUG) -Wall

LIBOPENCV = `pkg-config opencv --cflags --libs`
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec libswscale --cflags --libs`
LIBS=-Lvendor/lib -lpthread -lleveldb
#-static
HEADERS=-Iinc -Ivendor/include

#SQLITE_SRC=vendor/sqlite/sqlite3.
#SQLITE=sqlite3.o
SQLITE_SRC=
SQLITE=

all: evmgr evpuller evpusher evslicer evmlmotion

# sqlite C object
sqlite3.o: vendor/sqlite/sqlite3.c
	gcc -D SQLITE_THREADSAFE=1 -c vendor/sqlite/sqlite3.c

evmgr: evmgr.cpp database.cpp inc/common.hpp inc/database.h inc/zmqhelper.hpp inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evmgr evmgr.cpp $(SQLITE) database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpuller: evpuller.cpp database.cpp inc/common.hpp inc/database.h inc/zmqhelper.hpp inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evpuller evpuller.cpp $(SQLITE) database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpusher: evpusher.cpp inc/common.hpp inc/tinythread.hpp inc/database.h inc/zmqhelper.hpp  database.cpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evpusher evpusher.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evslicer: evslicer.cpp inc/common.hpp inc/tinythread.hpp inc/database.h inc/zmqhelper.hpp  database.cpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evslicer evslicer.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evmlmotion: evmlmotion.cpp inc/common.hpp inc/avcvhelpers.hpp inc/database.h inc/zmqhelper.hpp  inc/tinythread.hpp database.cpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evmlmotion evmlmotion.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) $(LIBOPENCV) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evdaemon: evdaemon.cpp inc/common.hpp inc/database.h inc/zmqhelper.hpp  inc/tinythread.hpp database.cpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evdaemon evdaemon.cpp database.cpp $(SQLITE) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(LD_FLAGS)

cvsample: cvsample.cpp
	$(CPP) $(CPPFLAGS) -o cvsample cvsample.cpp $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

.PHONY: clean
clean:
	rm -fr evmgr evpuller evpusher evslicer evmlmotion evdaemon *.dSYM *.out *.o

.PHONY: zmq
zmq:
	cd vendor/libzmq && ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor --enable-drafts
	cd vendor/libzmq && make clean && make -j 4 && make install

.PHONY: leveldb
leveldb:
	cd vendor/leveldb && mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=$(CURDIR)/vendor .. && make -j && make install
