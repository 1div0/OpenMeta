cmake_minimum_required(VERSION 3.20)
project(googletest_shim LANGUAGES CXX)

set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
find_package(GTest CONFIG REQUIRED)

