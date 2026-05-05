/*
 * SkiaRenderer: Skia GPU context and drawing for Vulkan swapchain surfaces.
 * Owns GrDirectContext, paint state, and provides draw/flush operations.
 */

#pragma once

#include "KawaseBlurFilter.hpp"
#include "Movie.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
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
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
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
    void clearInputQueue();
    void processInputEvent(int key, bool pressed);
    void setShowFps(bool show) { fShowFps = show; }
    void setSkipGridForeground(bool skip) { fSkipGridForeground = skip; }

    bool isScrolling() const { return fIsScrolling; }
    int focusIndex() const { return fFocusIndex; }
    bool detailMode() const { return fDetailMode; }

private:
    enum class PosterState {
        Empty,
        Loading,
        Ready,
        Failed,
    };

    struct PosterSlot {
        sk_sp<SkImage> fGridImage;
        sk_sp<SkImage> fBackgroundImage;
        sk_sp<SkImage> fDetailImage;
        sk_sp<SkImage> fDetailRaster;
        PosterState fState = PosterState::Empty;
        double fLastUsed = 0.0;
        uint32_t fLoadGeneration = 0;
    };

    struct DecodeJob {
        int fMovieIndex = -1;
        uint32_t fGeneration = 0;
    };

    struct DecodedCompletion {
        int fMovieIndex = -1;
        uint32_t fGeneration = 0;
        bool fSuccess = false;
        std::unique_ptr<SkBitmap> fBitmap;
    };

    sk_sp<GrDirectContext> fContext;
    SkPaint fTitlePaint;
    SkPaint fSelectionPaint;
    SkPaint fDimPaint;
    MovieDatabase fMovies;
    std::vector<PosterSlot> fPosterSlots;
    sk_sp<SkImage> fPosterPlaceholder;
    sk_sp<SkTypeface> fTypeface;
    SkFont fTitleFont;
    SkFont fDetailTitleFont;
    SkFont fDetailBodyFont;
    SkFont fDetailMetaFont;
    SkPaint fDetailTextPaint;
    SkPaint fFpsPaint;
    SkColorMatrix fMatrix;
    KawaseBlurFilter fBackgroundBlurFilter;
    sk_sp<SkImage> fBlurredCurrentBackground;
    sk_sp<SkImage> fBlurredPreviousBackground;
    int fBlurredCurrentIndex = -1;
    int fBlurredPreviousIndex = -1;
    uint32_t fBlurredCurrentGeneration = 0;
    uint32_t fBlurredPreviousGeneration = 0;
    uint32_t fBlurredCurrentSourceImageId = 0;
    uint32_t fBlurredPreviousSourceImageId = 0;
    int fBlurCacheWidth = 0;
    int fBlurCacheHeight = 0;

    std::mutex fDecodeMutex;
    std::condition_variable fDecodeCv;
    std::deque<DecodeJob> fPendingJobs;
    std::deque<DecodedCompletion> fCompletedDecodes;
    std::thread fDecodeThread;
    std::atomic<bool> fDecodeThreadExit{false};

    struct TitleCache {
        std::string text;
        std::string fullText;
        sk_sp<SkTextBlob> blob;
        float width = 0.0f;
        float textWidth = 0.0f;
    };
    std::vector<TitleCache> fTitleCache;
    float fCachedCellW = 0.0f;
    float fCachedUiScale = -1.0f;
    int fLayoutWidth = 0;
    int fLayoutHeight = 0;
    int fTileTargetWidth = 0;
    int fTileTargetHeight = 0;

    static constexpr float kDesignWidth = 1280.0f;
    static constexpr float kDesignHeight = 720.0f;
    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 3;
    static constexpr float kScrollDuration = 0.42f;
    static constexpr float kTitleFontSize = 28.0f;
    static constexpr float kTitleSpace = 32.0f;
    static constexpr float kPadding = 8.0f;
    static constexpr float kBackgroundFadeDuration = 0.50f;
    static constexpr uint32_t kBackgroundBlurRadius = 16;
    static constexpr uint8_t kBackgroundDimAlpha = 150;
    static constexpr float kBlurHoleInset = 1.5f;
    static constexpr float kCornerRadius = 12.0f;
    static constexpr float kSelectionOffset = 0.0f;
    static constexpr float kSelectionStrokeWidth = 3.0f;
    static constexpr float kTextScrollSpeed = 60.0f;
    static constexpr float kTextScrollPauseDuration = 2.5f;
    static constexpr float kScrollingTextClipAscent = 20.0f;
    static constexpr float kScrollingTextClipHeight = 40.0f;

    static constexpr double kPosterEvictIdleSeconds = 20.0;
    static constexpr size_t kMaxResidentPosters = 24;
    static constexpr size_t kMaxPendingDecodeJobs = 16;
    static constexpr size_t kMaxGpuResourceCacheBytes = 256u * 1024u * 1024u;

    static constexpr int kLoadingPlaceholderWidth = 960;
    static constexpr int kLoadingPlaceholderHeight = 540;
    static constexpr float kLoadingPlaceholderFontSize = 48.0f;
    static constexpr float kTileOversample = 1.1f;
    static constexpr float kBackgroundHalfScale = 0.5f;

    int fFocusIndex = 0;
    int fScrollOffset = 0;
    int fTargetOffset = 0;
    float fScrollProgress = 0.0f;
    bool fIsScrolling = false;
    float fScrollStartTime = 0.0f;
    bool fScrollingDown = true;
    int fBackgroundIndex = -1;
    int fBackgroundPrevIndex = -1;
    float fBackgroundFadeStartTime = 0.0f;
    float fBackgroundFadeProgress = 1.0f;
    bool fBackgroundFading = false;

    float fScrollingTextOffset = 0.0f;
    float fScrollingTextStartTime = 0.0f;
    bool fIsTextScrolling = false;
    bool fShowFps = false;
    float fLastFrameTime = -1.0f;
    float fSmoothedFps = 0.0f;
    bool fSkipGridForeground = false;

    std::queue<std::pair<int, bool>> fInputQueue;
    std::mutex fInputMutex;

    static float layoutScale(int width, int height);

    void rebuildTitleCache(float cellW);
    void finishScroll();
    float easeInOut(float t) const;
    static std::string ellipsizeText(const std::string& text, float maxWidth, SkFont& font);
    void drawDetailView(SkCanvas* canvas, int width, int height);
    void drawFpsOverlay(SkCanvas* canvas, float uiScale);
    void drawScrollingText(SkCanvas* canvas, const std::string& text, float x, float y, float maxWidth,
                           SkPaint& paint, float time, float uiScale);
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
    static constexpr float kDetailBodyLineGap = 4.0f;

    bool fDetailMode = false;
    int fDetailIndex = 0;

    bool makeLoadingPlaceholder();
    void decodeThreadMain();
    void drainDecodeCompletions();
    void enqueueDecodeJob(int movieIndex, uint32_t generation);
    void trimDecodeJobQueueLocked();
    void requestPosterDecode(int movieIndex);
    void updatePosterCache(double now);
    void evictPosterCache(double now, const std::vector<int>& touched);
    sk_sp<SkImage> posterForGrid(int movieIndex) const;
    sk_sp<SkImage> posterForBackground(int movieIndex) const;
    sk_sp<SkImage> posterForDetail(int movieIndex);
    static SkISize computeTileTargetSize(int width, int height);
    void updateTileTargetSize(int width, int height);
    sk_sp<SkImage> makeScaledImage(const SkImage* source, int targetWidth, int targetHeight) const;
    sk_sp<SkImage> uploadImageToGpu(const sk_sp<SkImage>& source) const;
    void invalidateBackgroundBlurCache();
    void invalidateBackgroundBlurCacheForIndex(int movieIndex);
    uint32_t backgroundGenerationForIndex(int movieIndex) const;
    sk_sp<SkImage> buildBlurredBackground(const sk_sp<SkImage>& source, int width, int height) const;
    sk_sp<SkImage> ensureBlurredBackgroundCached(bool previousSlot, int movieIndex, int width, int height);
};
