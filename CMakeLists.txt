cmake_minimum_required(VERSION 3.16)
project(TTY LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tty STATIC
    src/TTY.cpp
    src/rgb_protocol.cpp
)
target_include_directories(tty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(tty PUBLIC Threads::Threads)
endif()

add_executable(breath_led examples/breath_led.cpp)
target_link_libraries(breath_led PRIVATE tty)

if(MSVC)
    target_compile_options(tty PRIVATE /W4)
    target_compile_options(breath_led PRIVATE /W4)
else()
    target_compile_options(tty PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(breath_led PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(protocol_test tests/protocol_test.cpp)
    target_link_libraries(protocol_test PRIVATE tty)
    add_test(NAME protocol_test COMMAND protocol_test)
endif()