cmake_minimum_required(VERSION 4.0)
project(iris)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure x64 architecture
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "Configuring for 64-bit build")
endif()

# Build as a Windows GUI application (no console window)
add_executable(iris WIN32 main.cpp)

# Link required Windows libraries
target_link_libraries(iris PRIVATE shell32)
