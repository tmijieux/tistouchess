cmake_minimum_required(VERSION 3.10)

project(tistouchess CXX)

option(WITH_SDL "enable sdl ui" OFF)

list(APPEND
  CMAKE_MODULE_PATH
  "${PROJECT_SOURCE_DIR}/cmake/"
  "${PROJECT_SOURCE_DIR}/SDL2-2.26.1/cmake/"
  "${PROJECT_SOURCE_DIR}/SDL2_image-2.6.2/cmake/"
  "${PROJECT_SOURCE_DIR}/SDL2_ttf-2.20.1/cmake/"
)

if (WITH_SDL AND WIN32)
  include("sdl2-config")
  include("sdl2_image-config")
  include("sdl2_ttf-config")
elseif(WITH_SDL)
  find_package(SDL2 REQUIRED)
  find_package(SDL2_IMAGE REQUIRED)
  find_package(SDL2_TTF REQUIRED)
endif()

add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(src)
