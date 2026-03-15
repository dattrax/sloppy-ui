# AGENTS.md - Guidelines for Agentic Coding

## Build & Test Commands

### Build
```bash
cd /home/jim/vulkan_skia/build
cmake --build . -j$(nproc)
```

### Run Application
```bash
cd /home/jim/vulkan_skia/build
./skia_vulkan_test
```
Note: Run from build directory or ensure assets/ and movies.json are accessible via symlinks.

### Clean Build
```bash
cd /home/jim/vulkan_skia/build
rm -rf *
cmake ..
cmake --build . -j$(nproc)
```

### Test
This project has no automated test suite. Manual testing via running the application.

## Code Style Guidelines

### Imports & Includes
- Include project headers first: `#include "MyClass.hpp"`
- Skia includes use relative paths from Skia root: `#include "include/core/SkCanvas.h"`
- Standard library includes come last, grouped by category
- Use `sk_sp<T>` for smart pointers to Skia objects
- Use `std::unique_ptr<T>` for standard ownership semantics

### Naming Conventions
- **Classes**: PascalCase (e.g., `SkiaRenderer`, `MovieDatabase`)
- **Member variables**: Lowercase with `f` prefix (e.g., `fContext`, `fTypeface`, `fMovies`)
- **Constants**: `kPascalCase` for static constexpr (e.g., `kGridCols`, `kScrollDuration`)
- **Functions**: camelCase (e.g., `ellipsizeText`, `easeInOut`, `loadFromFile`)
- **Parameters**: camelCase with descriptive names (e.g., `info`, `signalSemaphore`)

### Formatting
- 4-space indentation
- Brace style: K&R (opening brace on same line)
- Maximum line length: ~100 characters
- Empty lines between logical sections
- Single blank line before `private:` and `public:` sections

### Error Handling
- Return `bool` for operations that can fail (e.g., `create()`, `loadFromFile()`)
- Use `fprintf(stderr, "Error message\n")` for error reporting
- Check for NULL/nullptr before dereferencing pointers
- Clean up resources on failure paths (e.g., call `destroy()` before returning false)
- Use `if (!object)` pattern for sk_sp and smart pointer checks

### Skia-Specific Patterns
- Use `sk_sp<T>` for all Skia object ownership (auto-reference counting)
- Use `SkPaint` for all drawing operations with `setAntiAlias(true)`
- Use `SkTextBlob` for text rendering (not `drawText` directly)
- Use `SkColorSetA()` for alpha-blended colors
- Use `SK_Color*` constants for common colors (e.g., `SK_ColorWHITE`, `SK_ColorBLACK`)
- Prefer `SkRect::MakeXYWH()` over manual construction
- Use `SkRRect::MakeRectXY()` for rounded rectangles

### Vulkan/Skia Integration
- Use `GrDirectContext` for GPU rendering context
- Flush and submit after drawing: `fContext->flush()` then `fContext->submit()`
- Use `GrBackendSemaphore` for synchronization
- Handle `VK_SUBOPTIMAL_KHR` and `VK_ERROR_OUT_OF_DATE_KHR` gracefully

### Resource Management
- Call `destroy()` in destructor to clean up resources
- Clear vectors and reset smart pointers in cleanup methods
- Use `reserve()` for vectors when size is known
- Use `std::move()` for transferring ownership of sk_sp objects

### Comments
- File headers describe purpose and prerequisites
- No inline comments for obvious code
- Debug output uses `fprintf(stderr, "DEBUG: ...")` during development

### Font Loading Pattern
```cpp
fTypeface = SkFontMgr::RefEmpty()->makeFromFile("path/to/font.ttf");
if (!fTypeface) {
    fTypeface = SkFontMgr::RefEmpty()->makeFromFile("fallback/path.ttf");
}
if (!fTypeface) {
    fTypeface = SkFontMgr::RefEmpty()->makeFromFile("/usr/share/fonts/...");
}
```

### Text Rendering with Ellipsis
- Calculate font size relative to container width
- Use helper function to truncate text with "..." if too long
- Center text horizontally: `x + width * 0.5f`
- Use `SkTextBlob::MakeFromText()` for UTF-8 text
