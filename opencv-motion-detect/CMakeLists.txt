#
#INFO: DRAFT, not completed
#
#

cmake_minimum_required (VERSION 3.0) 
project (evsuits)
add_definitions(-std=c++1z)

FIND_PACKAGE(OpenCV 4.0.0 REQUIRED)

include_directories(inc vendor/include)
link_directories(vendor/lib)

set(COMMON_SOURCES file1.cpp file2.cpp)
set(TARGET_SOURCES main.cpp)
set(TEST_SOURCES  run_tests.cpp test_file1.cpp test_file2.cpp)
add_library(database database.cpp hello.h)

add_executable(evmgr evmgr.cpp)
target_link_libraries(evmgr )
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")

find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)

find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)

find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_library(AVUTIL_LIBRARY avutil)

find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
find_library(AVDEVICE_LIBRARY avdevice)

add_executable(decode_encode main.cpp)
target_include_directories(decode_encode PRIVATE ${AVCODEC_INCLUDE_DIR} ${AVFORMAT_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR} ${AVDEVICE_INCLUDE_DIR})
target_link_libraries(decode_encode PRIVATE ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${AVDEVICE_LIBRARY})