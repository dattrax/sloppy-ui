# Sloppy UI - Skia Vulkan application.
# Prerequisites:
#   1. Build Skia with Vulkan: bin/gn gen out/Release --args='skia_use_vulkan=true is_official_build=true' && ninja -C out/Release skia'
#   2. Sync deps so VMA is available: python3 tools/git-sync-deps (from Skia root)
#   3. Install Vulkan SDK / libvulkan-dev on your system
#   4. Install GLFW: glfw-devel (Fedora) or libglfw3-dev (Debian) for windowed display

cmake_minimum_required(VERSION 3.16)
project(sloppy_ui CXX)

set(CMAKE_CXX_STANDARD 20)

set(SLOPPY_UI_BUILD_MODE "WINDOWED" CACHE STRING "Build mode: WINDOWED or DIRECT_TO_DISPLAY")
set_property(CACHE SLOPPY_UI_BUILD_MODE PROPERTY STRINGS WINDOWED DIRECT_TO_DISPLAY)
string(TOUPPER "${SLOPPY_UI_BUILD_MODE}" SLOPPY_UI_BUILD_MODE)
if(NOT SLOPPY_UI_BUILD_MODE STREQUAL "WINDOWED" AND
   NOT SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
  message(FATAL_ERROR
    "Invalid SLOPPY_UI_BUILD_MODE='${SLOPPY_UI_BUILD_MODE}'. "
    "Expected WINDOWED or DIRECT_TO_DISPLAY.")
endif()

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
  set(SKIA_BUILD_DIR "${SKIA_ROOT}/out/Release")
endif()

find_package(Vulkan REQUIRED)
include(FetchContent)
FetchContent_Declare(fetch_vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG v1.3.225
)
FetchContent_MakeAvailable(fetch_vk_bootstrap)

find_package(Boost REQUIRED COMPONENTS json)
if(NOT SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
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

if(SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
  add_compile_definitions(
    SLOPPY_UI_DIRECT_TO_DISPLAY=1
    VK_USE_PLATFORM_DISPLAY_KHR
  )
else()
  add_compile_definitions(SLOPPY_UI_DIRECT_TO_DISPLAY=0)
endif()

# Main executable
add_executable(sloppy_ui
  sloppy_ui.cpp
  src/SkiaRenderer.cpp
  src/SkiaRendererDetail.cpp
  src/SkiaRendererPosterCache.cpp
  src/BlurBackgroundMesh.cpp
  src/KawaseBlurFilter.cpp
  src/DirectInputPluginLoader.cpp
  src/Swapchain.cpp
  src/SwapchainImage.cpp
  src/Movie.cpp
)

if(NOT SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
  target_sources(sloppy_ui PRIVATE src/InputProcessor.cpp)
endif()

target_include_directories(sloppy_ui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(sloppy_ui SYSTEM PRIVATE
  ${SKIA_INCLUDES}
)

if(NOT SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
  target_include_directories(sloppy_ui SYSTEM PRIVATE ${glfw3_INCLUDE_DIRS})
endif()

set(SKIA_LIB_NAMES "libskia.a" "libskia.so" "skia.lib")
set(SKIA_LIB_PATH "")
foreach(LIB_NAME ${SKIA_LIB_NAMES})
  set(CANDIDATE "${SKIA_BUILD_DIR}/${LIB_NAME}")
  if(EXISTS "${CANDIDATE}")
    set(SKIA_LIB_PATH "${CANDIDATE}")
    break()
  endif()
endforeach()

target_link_libraries(sloppy_ui PRIVATE
  ${SKIA_LIB_PATH}
  Vulkan::Vulkan
  Boost::json
  vk-bootstrap::vk-bootstrap
  ${CMAKE_DL_LIBS}
)

if(NOT SLOPPY_UI_BUILD_MODE STREQUAL "DIRECT_TO_DISPLAY")
  target_link_libraries(sloppy_ui PRIVATE ${glfw3_LIBRARIES})
endif()

# Symlink assets and movies.json from source into build dir for run-from-build convenience
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/assets")
  file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets" SYMBOLIC)
  message(STATUS "Created symlink: build/assets -> source/assets")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/movies.json")
  file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/movies.json" "${CMAKE_CURRENT_BINARY_DIR}/movies.json" SYMBOLIC)
  message(STATUS "Created symlink: build/movies.json -> source/movies.json")
endif()

message(STATUS "Skia root: ${SKIA_ROOT}")
message(STATUS "Skia build: ${SKIA_BUILD_DIR}")
message(STATUS "Skia library: ${SKIA_LIB_PATH}")
message(STATUS "Sloppy UI build mode: ${SLOPPY_UI_BUILD_MODE}")
