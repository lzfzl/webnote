cmake_minimum_required(VERSION 3.10)
project(WebSerber)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug) 
include_directories(include)

set(SOURCES
    src/main.cpp
    src/webserver.cpp
    src/httphandler.cpp
)
add_executable(webserver ${SOURCES})