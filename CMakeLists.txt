cmake_minimum_required(VERSION 3.16)

project(TinyShellOSProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "This Tiny Shell project uses Windows process APIs and must be built on Windows.")
endif()

add_executable(myShell src/main.cpp)
add_executable(ascii_worker src/ascii_worker.cpp)

if(MINGW)
    target_compile_options(myShell PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(ascii_worker PRIVATE -Wall -Wextra -pedantic)
elseif(MSVC)
    target_compile_options(myShell PRIVATE /W4 /permissive-)
    target_compile_options(ascii_worker PRIVATE /W4 /permissive-)
endif()

install(TARGETS myShell ascii_worker RUNTIME DESTINATION .)
install(DIRECTORY scripts/ DESTINATION scripts FILES_MATCHING PATTERN "*")
