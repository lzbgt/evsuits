# cmake needs this line
cmake_minimum_required(VERSION 3.1)

set (CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH C:/Users/lzbgt/work/github.com/install)
endif()

# Define project name
project(yolocv)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()


# Find OpenCV, you may need to set OpenCV_DIR variable
# to the absolute path to the directory containing OpenCVConfig.cmake file
# via the command line or GUI

set(OpenCV_DATA_PATH C:/Users/lzbgt/work/github.com/opencv/data/haarcascades)
set(OpenCV_DIR C:/Users/lzbgt/work/github.com/opencv/install/x64/vc16/lib)
FIND_PACKAGE(OpenCV REQUIRED)

# If the package has been found, several variables will
# be set, you can find the full list with descriptions
# in the OpenCVConfig.cmake file.
# Print some message showing some of them
message(STATUS "OpenCV library status:")
message(STATUS "    config: ${OpenCV_DIR}")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "    install path: ${OpenCV_INSTALL_PATH}")

include_directories(C:/Users/lzbgt/work/github.com/install/include)
link_directories(C:/Users/lzbgt/work/github.com/install/lib)

# Declare the executable tacrget built from your sources
add_executable(${PROJECT_NAME} main.cpp) 

# Link your application with OpenCV libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS} libzmq)



install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE})
install(IMPORTED_RUNTIME_ARTIFACTS ${OpenCV_LIBS} DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE})