cmake_minimum_required(VERSION 3.20)
project(csp_app C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build Type" FORCE)
endif()

include(FetchContent)

# libcsp itself doesn't need the samples/unit tests/python bindings for our purposes.
set(CSP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(CSP_ENABLE_PYTHON3_BINDINGS OFF CACHE BOOL "" FORCE)
# Static, so the resulting binary runs standalone without LD_LIBRARY_PATH tricks.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  libcsp
  GIT_REPOSITORY https://github.com/libcsp/libcsp.git
  GIT_TAG        v2.1
)
FetchContent_MakeAvailable(libcsp)

find_package(Threads REQUIRED)

add_executable(csp_app src/main.c)
target_link_libraries(csp_app PRIVATE csp Threads::Threads)
