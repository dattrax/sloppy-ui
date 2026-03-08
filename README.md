# Skia Vulkan EPG

WARNING - WHEN CLONING YOU MUST HAVE gitlfs INSTALLED.  If not the assets will not
download correctly and the app will not render correctly.

Check in a filemanager before continuing.

## Prerequisites

Fedora/RHEL development tools:

   ```bash
   sudo dnf group install development-tools
   sudo dnf group install c-development
   ```

1. **Skia built with Vulkan** (from Skia repo root):
   ```bash
   python3 tools/git-sync-deps   # fetch deps (e.g. VulkanMemoryAllocator)
   bin/gn gen out/Release --args='skia_use_vulkan=true is_official_build=true'
   ninja -C out/Release skia
   ```

2. **Vulkan SDK / dev packages**
   ```bash
   sudo dnf install vulkan-devel
   ```

3. **CMake 3.16+** and a C++20-capable compiler (e.g. Clang 21+, GCC 11+).

## Build and run

Out-of-tree build: run from a build directory (or any directory) and pass the path to this repo as the source. Set **`SKIA_ROOT`** to your Skia source tree (built with Vulkan as in Prerequisites):

```bash
cmake -B build -DSKIA_ROOT=/path/to/skia /path/to/this-repo
cmake --build build
./build/skia_vulkan_test
```

**CMake variables:** `SKIA_ROOT` (required). `VMA_DIR` – VulkanMemoryAllocator root (default: `$SKIA_ROOT/third_party/externals/vulkanmemoryallocator`).

## Layout

- **`CMakeLists.txt`** – Finds Vulkan, adds Skia include dirs, compiles `skia_vulkan_test.cpp` and Skia helper sources (`VkTestUtils.cpp`, `VkTestMemoryAllocator.cpp`, `LoadDynamicLibrary_*`), links `libskia` from the GN build.
- **`skia_vulkan_test.cpp`** – Creates Vulkan instance/device via Skia’s test helper, builds a Vulkan `GrDirectContext`, creates an offscreen `SkSurface`, draws, flushes/submits, then tears down.

You can use this as a template for a larger app or for debugging the Skia Vulkan backend.
