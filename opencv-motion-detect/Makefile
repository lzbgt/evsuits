CC = gcc
CPP = g++
CPPFLAGS = -g -Wall -std=gnu++1z
CFLAGS = -g -Wall

LIBOPENCV = `pkg-config opencv --cflags --libs`
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec libswscale --cflags --libs`
LIBS=-Ivendor/lib -lpthread -lleveldb
SQLITE=vendor/sqlite/sqlite3.c
HEADERS=-Iinc -Ivendor/include
#SQLITE=sqlite3.o
SQLITE=

all: evmgr evpuller evpusher evslicer evmlmotion

# sqlite C object
sqlite3.o: vendor/sqlite/sqlite3.c
	gcc -D SQLITE_THREADSAFE=1 -c vendor/sqlite/sqlite3.c

evmgr: evmgr.cpp database.cpp inc/common.hpp inc/database.h inc/zmqhelper.hpp inc/tinythread.hpp $(SQLITE)
	$(CPP) $(CPPFLAGS) -o evmgr evmgr.cpp $(SQLITE) database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpuller: evpuller.cpp database.cpp inc/common.hpp inc/database.h inc/zmqhelper.hpp inc/tinythread.hpp $(SQLITE)
	$(CPP) $(CPPFLAGS) -o evpuller evpuller.cpp $(SQLITE) database.cpp $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpusher: evpusher.cpp inc/common.hpp inc/tinythread.hpp inc/database.h inc/zmqhelper.hpp  database.cpp $(SQLITE)
	$(CPP) $(CPPFLAGS) -o evpusher evpusher.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evslicer: evslicer.cpp inc/common.hpp inc/tinythread.hpp inc/database.h inc/zmqhelper.hpp  database.cpp $(SQLITE)
	$(CPP) $(CPPFLAGS) -o evslicer evslicer.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evmlmotion: evmlmotion.cpp inc/common.hpp inc/avcvhelpers.hpp inc/database.h inc/zmqhelper.hpp  inc/tinythread.hpp database.cpp $(SQLITE)
	$(CPP) $(CPPFLAGS) -o evmlmotion evmlmotion.cpp database.cpp $(SQLITE) $(LIBFFMPEG) $(HEADERS) $(LIBOPENCV) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc`  $(LIBS)

rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(LIBS)

cvsample: cvsample.cpp
	$(CPP) $(CPPFLAGS) -o cvsample cvsample.cpp $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

.PHONY: clean
clean:
	rm -fr evmgr evpuller evpusher evslicer evmlmotion *.dSYM *.out *.o

.PHONY: rebuildzmq
rebuildzmq:
	cd vendor/libzmq && make clean && ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor --enable-drafts
	cd vendor/libzmq && make -j 4 && make install

.PHONY: leveldb
leveldb:
	cd vendor/leveldb && mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=$(CURDIR)/vendor && make -j && make install

.PHONY: libzmq
libzmq:
	cd vendor/libzmq && [ ! -f $(CURDIR)/vendor/lib/pkgconfig/libzmq.pc ] || ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor --enable-drafts
	cd vendor/libzmq && make -j 4 && make install