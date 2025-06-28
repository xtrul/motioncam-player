# Minimal wrapper that calls the top-level CMakeLists
cmake_minimum_required(VERSION 3.22)
set(CMAKE_VERBOSE_MAKEFILE ON)

# Use the project definition from the repository root
include("${CMAKE_SOURCE_DIR}/../../../../CMakeLists.txt")
