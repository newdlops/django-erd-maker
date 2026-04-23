cmake_minimum_required(VERSION 3.20)
project(django-erd-ogdf-layout LANGUAGES CXX)

if(NOT DEFINED OGDF_SOURCE_DIR)
  message(FATAL_ERROR "OGDF_SOURCE_DIR must point at the extracted OGDF source directory.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OGDF_WARNING_ERRORS OFF CACHE BOOL "" FORCE)
set(OGDF_INCLUDE_CGAL OFF CACHE BOOL "" FORCE)
set(OGDF_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)

add_subdirectory(${OGDF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/ogdf)

add_executable(django-erd-ogdf-layout
  src/main.cpp
)

target_link_libraries(django-erd-ogdf-layout PRIVATE OGDF)
