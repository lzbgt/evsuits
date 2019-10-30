# ifeq ($(OS),Windows_NT)
#     CCFLAGS += -D WIN32
#     ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
#         CFLAGS
#     else
#         ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
#             CCFLAGS += -D AMD64
#         endif
#         ifeq ($(PROCESSOR_ARCHITECTURE),x86)
#             CCFLAGS += -D IA32
#         endif
#     endif
# else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LIBS += -ldl -lm -latomic
    endif
    ifeq ($(UNAME_S),Darwin)
        NOP=
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
# endif

#DEBUG=-g
CC = gcc
CPP = g++
CPPFLAGS = $(DEBUG) -Wall -std=gnu++1z
CFLAGS = $(DEBUG) -Wall

LIBOPENCV = `pkg-config opencv4 --cflags --libs`
#LIBOPENCV=-Ivendor/include/opencv4 -lopencv_video -lopencv_videoio -lopencv_imgproc  -lopencv_core -lopencv_dnn -lopencv_highgui
LIBFFMPEG = `pkg-config libavformat libavutil libavcodec libswscale --cflags --libs`
LIBS +=-Lvendor/lib -lpthread -lleveldb
#-static
HEADERS=-Iinc -Ivendor/include

#SQLITE_SRC=vendor/sqlite/sqlite3.
#SQLITE=sqlite3.o
SQLITE_SRC=
SQLITE=

all: evmgr evpuller evpusher evslicer evmlmotion evdaemon evcloudsvc

# sqlite C object
sqlite3.o: vendor/sqlite/sqlite3.c
	gcc -D SQLITE_THREADSAFE=1 -c vendor/sqlite/sqlite3.c
objs/database.o: database.cpp inc/database.h
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -c database.cpp -o objs/database.o $(HEADERS)
objs/zmqhelper.o: inc/zmqhelper.cpp inc/zmqhelper.hpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -c inc/zmqhelper.cpp -o objs/zmqhelper.o $(HEADERS)
objs/dirmon.o: dirmon.cpp dirmon.h
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -c dirmon.cpp -o objs/dirmon.o $(HEADERS)

objs/utils.o: inc/utils.cpp inc/utils.hpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -c inc/utils.cpp -o objs/utils.o $(HEADERS)

evmgr: evmgr.cpp database.cpp objs/utils.o inc/common.hpp objs/database.o objs/zmqhelper.o inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evmgr evmgr.cpp objs/utils.o $(SQLITE) objs/database.o objs/zmqhelper.o $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpuller: evpuller.cpp database.cpp inc/av_common.hpp objs/utils.o inc/common.hpp objs/database.o objs/zmqhelper.o inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evpuller evpuller.cpp objs/utils.o $(SQLITE) objs/database.o objs/zmqhelper.o $(HEADERS) $(LIBFFMPEG) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evpusher: evpusher.cpp inc/common.hpp inc/av_common.hpp objs/utils.o inc/tinythread.hpp objs/database.o objs/zmqhelper.o $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evpusher evpusher.cpp objs/database.o objs/utils.o objs/zmqhelper.o $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evslicer: evslicer.cpp inc/common.hpp inc/av_common.hpp postfile.cpp objs/utils.o objs/dirmon.o inc/tinythread.hpp objs/database.o objs/zmqhelper.o $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evslicer evslicer.cpp postfile.cpp objs/database.o  objs/dirmon.o objs/utils.o objs/zmqhelper.o $(SQLITE) $(LIBFFMPEG) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS) -lcurl -lfswatch

evmlmotion: evmlmotion.cpp inc/common.hpp inc/av_common.hpp objs/utils.o inc/avcvhelpers.hpp objs/database.o objs/zmqhelper.o  inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evmlmotion evmlmotion.cpp objs/database.o objs/utils.o objs/zmqhelper.o $(SQLITE) $(LIBFFMPEG) $(HEADERS) $(LIBOPENCV) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)
evmlmotion_d: evmlmotion.cpp inc/common.hpp inc/av_common.hpp objs/utils.o inc/avcvhelpers.hpp objs/database.o objs/zmqhelper.o  inc/tinythread.hpp $(SQLITE_SRC)
	$(CPP) $(CPPFLAGS) -DDEBUG $(LD_FLAGS) -o evmlmotion_d evmlmotion.cpp objs/database.o objs/utils.o objs/zmqhelper.o $(SQLITE) $(LIBFFMPEG) $(HEADERS) $(LIBOPENCV) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS)

evdaemon: evdaemon.cpp inc/common.hpp objs/utils.o objs/database.o objs/zmqhelper.o  inc/tinythread.hpp database.cpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evdaemon evdaemon.cpp objs/database.o objs/utils.o objs/zmqhelper.o $(SQLITE) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS) -lssh2

evcloudsvc: evcloudsvc.cpp objs/utils.o objs/database.o objs/zmqhelper.o  inc/tinythread.hpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o evcloudsvc evcloudsvc.cpp objs/utils.o objs/database.o objs/zmqhelper.o $(SQLITE) $(HEADERS) `pkg-config --cflags --libs vendor/lib/pkgconfig/libzmq.pc` $(LIBS) -lfmt

rtspr: rtsp-relay.cpp
	$(CPP) $(CPPFLAGS) $(LD_FLAGS) -o rtspr rtsp-relay.cpp $(LIBFFMPEG) $(LD_FLAGS)

cvsample: cvsample.cpp
	$(CPP) $(CPPFLAGS) -o cvsample cvsample.cpp $(LIBOPENCV)

mux: demuxing_decoding.c
	$(CC) $(CFLAGS) -o mux demuxing_decoding.c $(LIBFFMPEG)

.PHONY: clean
clean:
	rm -fr evmgr evpuller evpusher evslicer evmlmotion evdaemon evcloudsvc *.dSYM *.out *.o objs/*.o

#.PHONY: zmq
zmq:
	cd vendor/libzmq && ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor --enable-drafts
	cd vendor/libzmq && make clean && make -j 4 && make install

#.PHONY: leveldb
leveldb:
	cd vendor/leveldb && mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=$(CURDIR)/vendor .. && make -j && make install

#.PHONY: fmt
fmt:
	cd vendor/fmt && mkdir -p build && cd build && cmake -DCMAKE_INSTALL_PREFIX=$(CURDIR)/vendor -DFMT_TEST=OFF .. && make -j && make install

fswatch:
	cd vendor/fswatch && ./autogen.sh && ./configure --prefix=$(CURDIR)/vendor && make -j && make install

libcurl:
	cd vendor/curl && ./buildconf && ./configure --prefix=$(CURDIR)/vendor && make -j && make install
