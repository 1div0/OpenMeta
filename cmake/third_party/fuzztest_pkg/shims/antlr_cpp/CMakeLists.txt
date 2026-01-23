cmake_minimum_required(VERSION 3.20)
project(antlr_cpp_shim LANGUAGES CXX)

set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
find_package(antlr4-runtime CONFIG REQUIRED)

# FuzzTest's generated parser expects `${antlr_cpp_SOURCE_DIR}/runtime/src`.
if(DEFINED OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR AND EXISTS "${OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR}/runtime/src")
  set(antlr_cpp_SOURCE_DIR "${OPENMETA_ANTLR_CPP_REAL_SOURCE_DIR}" PARENT_SCOPE)
endif()

