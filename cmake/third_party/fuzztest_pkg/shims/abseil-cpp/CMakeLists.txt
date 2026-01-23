cmake_minimum_required(VERSION 3.20)
project(abseil_cpp_shim LANGUAGES CXX)

# Imported targets are directory-scoped by default in modern CMake; make them
# visible to the rest of the (FetchContent) build.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
find_package(absl CONFIG REQUIRED)

