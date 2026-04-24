# Sloppy UI

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
   bin/gn gen out/Release
      --args='skia_use_vulkan=true
             skia_use_gl=false
             is_official_build=true
             skia_use_libwebp_decode=false
             skia_use_libwebp_encode=false
             skia_use_system_libpng=false
             skia_use_system_freetype2=false
             skia_use_system_libjpeg_turbo=false
             skia_use_system_zlib=false
             skia_use_system_expat=false
             is_component_build=true'
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
./build/sloppy_ui
```

Build mode can be selected with `SLOPPY_UI_BUILD_MODE`:

```bash
# Default:
cmake -B build -DSKIA_ROOT=/path/to/skia -DSLOPPY_UI_BUILD_MODE=WINDOWED /path/to/this-repo

# Alternate build mode flag:
cmake -B build -DSKIA_ROOT=/path/to/skia -DSLOPPY_UI_BUILD_MODE=DIRECT_TO_DISPLAY /path/to/this-repo
```

For `DIRECT_TO_DISPLAY`, input is loaded at runtime from a shared plugin via `dlopen`.
The application looks for:

1. `SLOPPY_UI_INPUT_PLUGIN_PATH` (if set)
2. `libsloppy_input.so` via normal dynamic loader search paths (`LD_LIBRARY_PATH`, etc.)

If the plugin cannot be loaded, or required symbols are missing
(`sloppy_input_create`, `sloppy_input_poll_event`, `sloppy_input_destroy`),
startup fails with an error.

**CMake variables:** `SKIA_ROOT` (required). `VMA_DIR` – VulkanMemoryAllocator root (default: `$SKIA_ROOT/third_party/externals/vulkanmemoryallocator`).

## Layout

- **`CMakeLists.txt`** – Finds Vulkan, adds Skia include dirs, compiles `sloppy_ui.cpp` and Skia helper sources (`VkTestUtils.cpp`, `VkTestMemoryAllocator.cpp`, `LoadDynamicLibrary_*`), links `libskia` from the GN build.
- **`sloppy_ui.cpp`** – Creates Vulkan instance/device via Skia’s test helper, builds a Vulkan `GrDirectContext`, creates an offscreen `SkSurface`, draws, flushes/submits, then tears down.
- **`src/DirectInputPluginApi.h`** – C ABI for direct-display input plugins.
- **`src/DirectInputPluginLoader.*`** – `dlopen`/`dlsym` runtime loader for direct-display input.

You can use this as a template for a larger app or for debugging the Skia Vulkan backend.
