cmake_minimum_required(VERSION 3.20)
project(DarkEnginePortability LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

if(WIN32)
  set(DARK_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/winsrc/platform")
  set(DARK_PLATFORM_SOURCE "${DARK_PLATFORM_ROOT}/platform_services.cpp")
else()
  set(DARK_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/unixsrc/platform")
  set(DARK_PLATFORM_SOURCE "${DARK_PLATFORM_ROOT}/platform_services.cpp")
endif()

add_library(dark_platform STATIC
  ${DARK_PLATFORM_SOURCE}
  libsrc/cpptools/dynfunc.cpp
)
target_include_directories(dark_platform PUBLIC
  h
  libsrc/cpptools
  ${DARK_PLATFORM_ROOT}
)
if(NOT WIN32)
  target_link_libraries(dark_platform PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(dark_compression STATIC 3rdparty/blast/blast.c)
target_include_directories(dark_compression PUBLIC 3rdparty/blast)

# This target deliberately validates the portable OS boundary first. The full
# executable remains in src/dromed.vcxproj until the remaining engine libraries
# have been added without importing Windows providers into the common target.
