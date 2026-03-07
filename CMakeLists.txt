# CMake project to build a Skia Vulkan backend test.
# Prerequisites:
#   1. Build Skia with Vulkan: bin/gn gen out/Vulkan --args='skia_use_vulkan=true' && ninja -C out/Vulkan skia'
#   2. Sync deps so VMA is available: python3 tools/git-sync-deps (from Skia root)
#   3. Install Vulkan SDK / libvulkan-dev on your system
#   4. Install GLFW: glfw-devel (Fedora) or libglfw3-dev (Debian) for windowed display

cmake_minimum_required(VERSION 3.16)
project(skia_vulkan_test CXX)

set(CMAKE_CXX_STANDARD 20)

# Skia root (allow -DSKIA_ROOT, then env, then parent of this directory)
if(DEFINED SKIA_ROOT)
  # use cache value from -DSKIA_ROOT
elseif(DEFINED ENV{SKIA_ROOT})
  set(SKIA_ROOT "$ENV{SKIA_ROOT}")
else()
  set(SKIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

# Skia build output
if(DEFINED SKIA_BUILD_DIR)
  # use cache value
elseif(DEFINED ENV{SKIA_BUILD_DIR})
  set(SKIA_BUILD_DIR "$ENV{SKIA_BUILD_DIR}")
else()
  set(SKIA_BUILD_DIR "${SKIA_ROOT}/out/Vulkan")
endif()

find_package(Vulkan REQUIRED)
find_package(Freetype REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(glfw3 QUIET glfw3)
endif()
if(NOT glfw3_FOUND)
  find_package(glfw3 QUIET)
endif()
if(NOT glfw3_FOUND)
  find_path(GLFW_INCLUDE_DIR NAMES GLFW/glfw3.h)
  find_library(GLFW_LIBRARY NAMES glfw)
  if(GLFW_INCLUDE_DIR AND GLFW_LIBRARY)
    set(glfw3_FOUND TRUE)
    set(glfw3_INCLUDE_DIRS ${GLFW_INCLUDE_DIR})
    set(glfw3_LIBRARIES ${GLFW_LIBRARY})
  endif()
endif()
if(NOT glfw3_FOUND)
  message(FATAL_ERROR "GLFW not found. Install glfw-devel (Fedora) or libglfw3-dev (Debian).")
endif()

# Include directories for Skia (public include + src for VulkanMemoryAllocatorPriv.h)
set(SKIA_INCLUDES
  ${SKIA_ROOT}
  ${SKIA_ROOT}/include
  ${SKIA_ROOT}/include/private
  ${SKIA_ROOT}/include/private/base
  ${SKIA_ROOT}/include/third_party/vulkan
  ${SKIA_ROOT}/src
  ${Vulkan_INCLUDE_DIRS}
)

# Defines matching Skia's Vulkan build
add_compile_definitions(
  SK_VULKAN
  SK_USE_INTERNAL_VULKAN_HEADERS
  SK_GANESH
  $<$<PLATFORM_ID:Linux>:SK_BUILD_FOR_UNIX>
)

# Main executable
add_executable(skia_vulkan_test
  skia_vulkan_test.cpp
)

target_include_directories(skia_vulkan_test PRIVATE ${SKIA_INCLUDES} ${glfw3_INCLUDE_DIRS})
set(SKIA_LIB_NAMES "libskia.a" "libskia.so" "skia.lib")
set(SKIA_LIB_PATH "")
foreach(LIB_NAME ${SKIA_LIB_NAMES})
  set(CANDIDATE "${SKIA_BUILD_DIR}/${LIB_NAME}")
  if(EXISTS "${CANDIDATE}")
    set(SKIA_LIB_PATH "${CANDIDATE}")
    break()
  endif()
endforeach()

target_link_libraries(skia_vulkan_test PRIVATE
  ${SKIA_LIB_PATH}
  Vulkan::Vulkan
  Freetype::Freetype
  GPUOpen::VulkanMemoryAllocator
  ${glfw3_LIBRARIES}
)

# On Linux we may need pthread and dl for Skia
if(UNIX AND NOT APPLE)
  target_link_libraries(skia_vulkan_test PRIVATE pthread dl)
endif()

message(STATUS "Skia root: ${SKIA_ROOT}")
message(STATUS "Skia build: ${SKIA_BUILD_DIR}")
message(STATUS "Skia library: ${SKIA_LIB_PATH}")
