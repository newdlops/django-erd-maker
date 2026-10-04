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
option(
  DJERD_OGDF_INCLUDE_CGAL
  "Enable OGDF's CGAL-backed geometric crossing minimizers"
  OFF
)
set(OGDF_INCLUDE_CGAL ${DJERD_OGDF_INCLUDE_CGAL} CACHE BOOL "" FORCE)
set(OGDF_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)

add_subdirectory(${OGDF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/ogdf)

# OGDF discovers these optional dependencies but its library target does not
# propagate them in every release.  Attach them explicitly so the geometric
# sources can see CGAL/GMP and OpenMP while compiling.
if(DJERD_OGDF_INCLUDE_CGAL)
  target_link_libraries(OGDF PUBLIC CGAL::CGAL CGAL::CGAL_Core OpenMP::OpenMP_CXX)
endif()

add_executable(django-erd-ogdf-layout
  src/main.cpp
  src/io.cpp
  src/clusterGraph.cpp
  src/canonicalCrossingMetrics.cpp
  src/crossingLowerBound.cpp
)

# main.cpp contains the full layout/post-processing pipeline in one large
# translation unit. Release optimization can make clang exceed the extension's
# bounded build memory budget even with a single compile job. OGDF itself stays
# optimized; compiling this orchestration unit at -O0 keeps compiler RSS
# predictable and avoids taking down the host during development builds.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "-O0")
endif()

target_link_libraries(django-erd-ogdf-layout PRIVATE OGDF)

add_executable(django-erd-planar-backbone-probe
  ../../scripts/erd-poc/ogdf_planar_backbone.cpp
)
target_link_libraries(django-erd-planar-backbone-probe PRIVATE OGDF)

add_executable(django-erd-general-layout-probe
  ../../scripts/erd-poc/ogdf_general_layout_probe.cpp
)
target_link_libraries(django-erd-general-layout-probe PRIVATE OGDF)

# OpenMP for parallel hot loops (cluster_graph cross-min, route detour,
# face raster). On Apple Silicon, prefer libomp from Homebrew.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(django-erd-ogdf-layout PRIVATE OpenMP::OpenMP_CXX)
  target_compile_definitions(django-erd-ogdf-layout PRIVATE DJERD_HAS_OPENMP=1)
  message(STATUS "OpenMP enabled for django-erd-ogdf-layout")
else()
  message(WARNING "OpenMP not found; binary will run single-threaded")
endif()
