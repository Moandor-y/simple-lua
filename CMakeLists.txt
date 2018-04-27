cmake_minimum_required(VERSION 3.10)
project(slua)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic-errors -Wall -Wextra")

include_directories(GSL/include)
include_directories(${LLVM_INCLUDE_DIRS})

add_definitions(${LLVM_DEFINITIONS})

add_library(slua_rt runtime/slua_rt.cpp)

add_executable(slua main.cpp lexer.cpp parser.cpp emitter.cpp)

llvm_map_components_to_libnames(llvm_libs support core irreader native ipo)

target_link_libraries(slua ${llvm_libs})
