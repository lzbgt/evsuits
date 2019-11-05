cmake_minimum_required (VERSION 3.0)
project (evsuits)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
endif()

add_definitions(-std=c++1z)

if("${VENDOR}" STREQUAL "")
    set(VENDOR "${CMAKE_SOURCE_DIR}/vendor")
endif()

#set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(ENV{PKG_CONFIG_PATH}  "${VENDOR}/lib/pkgconfig:${PKG_CONFIG_PATH}")
FIND_PACKAGE(PkgConfig REQUIRED)


if(MACOSX)
    include_directories("/usr/local/Cellar/libssh2/1.9.0_1/include")
    link_directories("/usr/local/Cellar/libssh2/1.9.0_1/lib")
    pkg_search_module(CV REQUIRED opencv4)
    MESSAGE("CV_VERSION: ${CV_VERSION}")
    list(APPEND SSH2LIB ssh2)
else()
    set(CV_INCLUDE_DIRS "${VENDOR}/include/opencv4")
    set(CV_LIBRARY_DIRS "${VENDOR}/lib")
    list(APPEND EXTRA_LIBS pthread x264 m z dl atomic lzma)
    list(APPEND SSH2LIB ssh2 gcrypt)
endif()

LIST(APPEND CV_LIBRARIES opencv_video opencv_videoio opencv_imgproc opencv_core opencv_dnn opencv_highgui)

pkg_search_module(FMT REQUIRED fmt)
pkg_search_module(ZMQ REQUIRED libzmq)
pkg_search_module(CURL REQUIRED libcurl)

pkg_search_module(AVF REQUIRED libavformat)
pkg_search_module(AVU REQUIRED libavutil)
pkg_search_module(AVC REQUIRED libavcodec)
pkg_search_module(AVS REQUIRED libswscale)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}" "inc" "${VENDOR}/include" "${CV_INCLUDE_DIRS}" "${AVF_INCLUDE_DIRS}" "${AVU_INCLUDE_DIRS}" "${AVC_INCLUDE_DIRS}" "${AVS_INCLUDE_DIRS}")
link_directories("vendor/lib" "${CV_LIBRARY_DIRS}" "${AVF_LIBRARY_DIRS}" "${AVC_LIBRARY_DIRS}" "${AVU_LIBRARY_DIRS}" "${AVS_LIBRARY_DIRS}")

add_library(database STATIC database.cpp)
add_library(zmqhelper STATIC inc/zmqhelper.cpp)
add_library(dirmon STATIC dirmon.cpp)
add_library(post STATIC postfile.cpp)
add_library(util STATIC inc/utils.cpp)

# order matters, local lib first
LIST(APPEND COMM_LIBS zmqhelper util fmt zmq)
LIST(APPEND AV_LIBS avformat swscale avcodec avutil swresample)

add_executable(evcloudsvc evcloudsvc.cpp)
target_link_libraries(evcloudsvc PUBLIC database leveldb ${COMM_LIBS} ${EXTRA_LIBS})

add_executable(evdaemon evdaemon.cpp)
target_link_libraries(evdaemon PUBLIC database leveldb ${COMM_LIBS} ${SSH2LIB}  ${EXTRA_LIBS})

add_executable(evmgr evmgr.cpp)
target_link_libraries(evmgr PUBLIC database leveldb ${AV_LIBS} ${COMM_LIBS} ${EXTRA_LIBS})

add_executable(evpuller evpuller.cpp)
target_link_libraries(evpuller PUBLIC ${COMM_LIBS} ${AV_LIBS} ${EXTRA_LIBS})

add_executable(evpusher evpusher.cpp)
target_link_libraries(evpusher PUBLIC ${COMM_LIBS} ${AV_LIBS} ${EXTRA_LIBS})

add_executable(evslicer evslicer.cpp)
target_link_libraries(evslicer PUBLIC dirmon post ${COMM_LIBS} ${AV_LIBS} curl fswatch  ${EXTRA_LIBS})

add_executable(evmlmotion evmlmotion.cpp)
target_link_libraries(evmlmotion PUBLIC ${COMM_LIBS} ${AV_LIBS} ${CV_LIBRARIES} ${EXTRA_LIBS})