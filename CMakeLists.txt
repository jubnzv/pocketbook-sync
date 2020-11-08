cmake_minimum_required(VERSION 3.8.2)
project(pbsync)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

if(WIN32)
  set(CMAKE_WIN32_EXECUTABLE ON)
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Sql)
add_executable(pbsync main.cpp)
target_link_libraries(pbsync Qt5::Core Qt5::Sql)
set_property(TARGET pbsync PROPERTY CXX_STANDARD 17)
target_compile_options(pbsync PUBLIC -fno-rtti)
