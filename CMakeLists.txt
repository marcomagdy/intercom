cmake_minimum_required(VERSION 3.20)

SET(CMAKE_CXX_STANDARD 20)

project(intercom VERSION 0.1 LANGUAGES CXX)

add_executable(${PROJECT_NAME} 
    TcpConnection.cpp
    main.cpp)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)

# Find PkgConfig module
find_package(PkgConfig REQUIRED)

# Use pkg-config to find PortAudio
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
# Link against PortAudio with full path
foreach(lib ${PORTAUDIO_LIBRARIES})
    find_library(${lib}_LIB ${lib} HINTS ${PORTAUDIO_LIBRARY_DIRS})
    if(${lib}_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${${lib}_LIB})
    else()
        # if the library is a framework, use the framework name
        target_link_libraries(${PROJECT_NAME} PRIVATE ${lib})
    endif()
endforeach()

# include directories from homebrew for portaudio
target_include_directories(${PROJECT_NAME} PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
