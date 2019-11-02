cmake_minimum_required (VERSION 3.0) 
project (evsuits)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
endif()

add_definitions(-std=c++1z)

#set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(ENV{PKG_CONFIG_PATH}  "${CMAKE_SOURCE_DIR}/vendor/lib/pkgconfig:${PKG_CONFIG_PATH}")
FIND_PACKAGE(PkgConfig REQUIRED)


if(MACOSX)
    include_directories("/usr/local/Cellar/libssh2/1.9.0_1/include")
    link_directories("/usr/local/Cellar/libssh2/1.9.0_1/lib")
    list(APPEND SSH2LIB ssh2)

    pkg_search_module(CV REQUIRED opencv4)
    MESSAGE("CV_VERSION: ${CV_VERSION}")
else()
    list(APPEND SSH2LIB ssh2 gcrypt)
    set(CV_INCLUDE_DIRS "vendor/include/opencv4")
    set(CV_LIBRARY_DIRS "vendor/lib")
    list(APPEND EXTRA_LIBS pthread x264 m z dl atomic)
endif()

LIST(APPEND CV_LIBRARIES opencv_video opencv_videoio opencv_imgproc opencv_core opencv_dnn opencv_highgui)

pkg_search_module(FMT REQUIRED fmt)
MESSAGE("FMT_VERSION: ${FMT_VERSION}")
pkg_search_module(ZMQ REQUIRED libzmq)
MESSAGE("ZMQ_VERSION: ${ZMQ_VERSION}")

pkg_search_module(CURL REQUIRED libcurl)
MESSAGE("CURL_VERSION: ${CURL_VERSION}")

pkg_search_module(AVF REQUIRED libavformat)
MESSAGE("AVF_VERSION: ${AVF_VERSION}")
pkg_search_module(AVU REQUIRED libavutil)
MESSAGE("AVU_VERSION: ${AVU_VERSION},")
pkg_search_module(AVC REQUIRED libavcodec)
MESSAGE("AVC_VERSION: ${AVC_VERSION}")
pkg_search_module(AVS REQUIRED libswscale)
MESSAGE("AVS_VERSION: ${AVS_VERSION}")

# if(AV_FOUND)
#     list(APPEND AV_INCLUDE_DIRS ${AVF_INCLUDE_DIR} ${AV_INCLUDE_DIR} ${AV_INCLUDE_DIR} ${AV_INCLUDE_DIR})
#     list(APPEND VA_LIBRARY_DIRS ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS})
#     list(APPEND VA_LIBRARIES ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS} ${AV_LIBRARY_DIRS})
# else()
#     message(FATAL_ERROR "${LIBAV_INCLUDE_DIR} ${LIBAV_LIBRARY} libavformat not available")
# endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}" "inc" "vendor/include" "${CV_INCLUDE_DIRS}" "${AVF_INCLUDE_DIRS}" "${AVU_INCLUDE_DIRS}" "${AVC_INCLUDE_DIRS}" "${AVS_INCLUDE_DIRS}")
link_directories("vendor/lib" "${CV_LIBRARY_DIRS}" "${AVF_LIBRARY_DIRS}" "${AVC_LIBRARY_DIRS}" "${AVU_LIBRARY_DIRS}" "${AVS_LIBRARY_DIRS}")

add_library(database STATIC database.cpp)
add_library(zmqhelper STATIC inc/zmqhelper.cpp)
add_library(dirmon STATIC dirmon.cpp)
add_library(post STATIC postfile.cpp)
add_library(util STATIC inc/utils.cpp)

add_executable(evcloudsvc evcloudsvc.cpp)
target_link_libraries(evcloudsvc PUBLIC zmq fmt leveldb database zmqhelper util ${EXTRA_LIBS})

add_executable(evdaemon evdaemon.cpp)
target_link_libraries(evdaemon PUBLIC zmq fmt leveldb ${SSH2LIB}  database zmqhelper util ${EXTRA_LIBS})

add_executable(evmgr evmgr.cpp)
target_link_libraries(evmgr PUBLIC zmq fmt leveldb avutil avformat database zmqhelper util ${EXTRA_LIBS})

add_executable(evpuller evpuller.cpp)
target_link_libraries(evpuller PUBLIC zmq fmt avformat swscale avcodec avutil swresample leveldb database zmqhelper util ${EXTRA_LIBS})

add_executable(evpusher evpusher.cpp)
target_link_libraries(evpusher PUBLIC zmq fmt leveldb avformat swscale avcodec avutil swresample database zmqhelper util ${EXTRA_LIBS})

add_executable(evslicer evslicer.cpp)
target_link_libraries(evslicer PUBLIC zmq curl fmt fswatch avformat swscale avcodec avutil swresample database zmqhelper dirmon post util ${EXTRA_LIBS})

add_executable(evmlmotion evmlmotion.cpp)
target_link_libraries(evmlmotion PUBLIC zmq fmt avformat swscale avcodec avutil swresample leveldb ${CV_LIBRARIES} database zmqhelper util ${EXTRA_LIBS})