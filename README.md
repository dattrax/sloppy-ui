# Skia Vulkan Backend – CMake Test

Minimal CMake project that builds a small test program using Skia’s **Vulkan** GPU backend. The test creates a Vulkan-backed `GrDirectContext`, draws to an `SkSurface` (clear + rect + circle), then flushes and submits.

## Prerequisites

1. **Skia built with Vulkan** (from Skia repo root):
   ```bash
   python3 tools/git-sync-deps   # fetch deps (e.g. VulkanMemoryAllocator)
   bin/gn gen out/Vulkan --args='skia_use_vulkan=true'
   ninja -C out/Vulkan skia
   ```

2. **Vulkan SDK / dev packages**  
   - Linux: `libvulkan-dev` (and driver, e.g. Mesa)  
   - Windows: [LunarG Vulkan SDK](https://vulkan.lunarg.com/)  
   - macOS: Vulkan via SwiftShader only (see Skia Vulkan docs).

3. **CMake 3.16+** and a C++20-capable compiler (e.g. Clang 21+, GCC 11+).

## Build and run

From this directory (`skia_vulkan_cmake`):

```bash
cmake -B build
cmake --build build
./build/skia_vulkan_test
```

Successful output: `Skia Vulkan backend test passed.`

## Options

- **`SKIA_ROOT`** – Skia source tree (default: parent of this directory).
- **`SKIA_BUILD_DIR`** – Where Skia was built (default: `$SKIA_ROOT/out/Vulkan`).
- **`VMA_DIR`** – VulkanMemoryAllocator root (default: `$SKIA_ROOT/third_party/externals/vulkanmemoryallocator`).

Example with custom paths:

```bash
cmake -B build -DSKIA_ROOT=/path/to/skia -DSKIA_BUILD_DIR=/path/to/skia/out/Vulkan
cmake --build build
```

## Layout

- **`CMakeLists.txt`** – Finds Vulkan, adds Skia include dirs, compiles `skia_vulkan_test.cpp` and Skia helper sources (`VkTestUtils.cpp`, `VkTestMemoryAllocator.cpp`, `LoadDynamicLibrary_*`), links `libskia` from the GN build.
- **`skia_vulkan_test.cpp`** – Creates Vulkan instance/device via Skia’s test helper, builds a Vulkan `GrDirectContext`, creates an offscreen `SkSurface`, draws, flushes/submits, then tears down.

You can use this as a template for a larger app or for debugging the Skia Vulkan backend.
