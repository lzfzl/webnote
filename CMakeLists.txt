cmake_minimum_required(VERSION 3.10)
project(WebSerber)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug) 
include_directories(include)

file(GLOB SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
add_executable(webserver ${SOURCES})