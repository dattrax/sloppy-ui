/*
 * SkiaRenderer: Skia GPU context and drawing for Vulkan swapchain surfaces.
 * Owns GrDirectContext, paint state, and provides draw/flush operations.
 */

#pragma once

#include "Movie.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkColorMatrix.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <cstdint>
#include <queue>
#include <mutex>
#include <utility>

class SkiaRenderer {
public:
    SkiaRenderer() = default;
    ~SkiaRenderer() { destroy(); }

    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;

    struct CreateInfo {
        skgpu::VulkanBackendContext* backendContext = nullptr;
        skgpu::VulkanExtensions* extensions = nullptr;
        VkPhysicalDeviceFeatures2* deviceFeatures2 = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        uint32_t instanceExtensionCount = 0;
        const char** instanceExtensionNames = nullptr;
        uint32_t deviceExtensionCount = 0;
        const char** deviceExtensionNames = nullptr;
    };

    bool create(const CreateInfo& info);
    void destroy();

    void draw(SkCanvas* canvas, int width, int height, float time);
    bool flushAndSubmit(SkSurface* surface, VkSemaphore signalSemaphore, uint32_t presentQueueIndex);

    GrDirectContext* context() const { return fContext.get(); }

    const MovieDatabase& movies() const { return fMovies; }

    void enqueueInputEvent(int key, bool pressed);
    bool pollInputEvent(std::pair<int, bool>& event);
    void processInputEvent(int key, bool pressed);

    int selectedRow() const { return fSelectedRow; }
    int selectedCol() const { return fSelectedCol; }

private:
    sk_sp<GrDirectContext> fContext;
    SkPaint fTitlePaint;
    SkPaint fSelectionPaint;
    SkPaint fDimPaint;
    MovieDatabase fMovies;
    std::vector<sk_sp<SkImage>> fPosterImages;
    sk_sp<SkTypeface> fTypeface;
    SkFont fTitleFont;
    SkFont fDetailTitleFont;
    SkFont fDetailBodyFont;
    SkFont fDetailMetaFont;
    SkPaint fDetailTextPaint;
    SkColorMatrix fMatrix;

    struct TitleCache {
        std::string text;
        std::string fullText;
        sk_sp<SkTextBlob> blob;
        float width = 0.0f;
        float textWidth = 0.0f;
    };
    std::vector<TitleCache> fTitleCache;
    float fCachedCellW = 0.0f;

    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 3;
    static constexpr float kScrollDuration = 0.25f;
    static constexpr float kTitleFontSize = 28.0f;
    static constexpr float kTitleSpace = 32.0f;
    static constexpr float kPadding = 8.0f;
    static constexpr float kCornerRadius = 12.0f;
    static constexpr float kSelectionOffset = 0.0f;
    static constexpr float kTextScrollSpeed = 60.0f;
    static constexpr float kTextScrollPauseDuration = 2.5f;

    int fSelectedRow = 0;
    int fSelectedCol = 0;
    int fScrollOffset = 0;
    int fTargetOffset = 0;
    float fScrollProgress = 0.0f;
    bool fIsScrolling = false;
    float fScrollStartTime = 0.0f;
    bool fScrollingDown = true;

    float fScrollingTextOffset = 0.0f;
    float fScrollingTextStartTime = 0.0f;
    bool fIsTextScrolling = false;

    std::queue<std::pair<int, bool>> fInputQueue;
    std::mutex fInputMutex;

    void rebuildTitleCache(float cellW);
    void finishScroll();
    float easeInOut(float t) const;
    static std::string ellipsizeText(const std::string& text, float maxWidth, SkFont& font);
    void drawScrollingText(SkCanvas* canvas, const std::string& text, float x, float y, float maxWidth, SkPaint& paint, float time);

    void drawDetailView(SkCanvas* canvas, int width, int height);
    static std::vector<std::string> wrapTextToLines(const std::string& text, float maxWidth, SkFont& font);
    static std::string formatDetailPrice(const Movie& movie);
    static std::string formatDetailMetadata(const Movie& movie);

    static constexpr float kDetailPanelFraction = 0.6f;
    static constexpr float kDetailFeatherFraction = 0.15f;
    static constexpr float kDetailTitleFontSize = 48.0f;
    static constexpr float kDetailBodyFontSize = 28.0f;
    static constexpr float kDetailMetaFontSize = 28.0f;
    static constexpr float kDetailPanelPadding = 24.0f;
    static constexpr float kDetailTitleBodyGap = 20.0f;
    static constexpr float kDetailBodyMetaGap = 20.0f;

    bool fDetailMode = false;
    int fDetailIndex = 0;
};
