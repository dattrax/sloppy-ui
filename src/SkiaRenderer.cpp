#include "SkiaRenderer.hpp"
#include "BlurBackgroundMesh.hpp"
#include "PlatformInput.hpp"
#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_empty.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <utility>

namespace {

SkSamplingOptions gridSampling() {
    return SkSamplingOptions(SkCubicResampler::Mitchell());
}

SkSamplingOptions backgroundSampling() {
    return SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone);
}

void updateSmoothedFps(float time, float& lastFrameTime, float& smoothedFps) {
    if (lastFrameTime >= 0.0f) {
        const float dt = time - lastFrameTime;
        if (dt > 0.0001f) {
            const float instantFps = 1.0f / dt;
            if (smoothedFps <= 0.0f) {
                smoothedFps = instantFps;
            } else {
                smoothedFps = smoothedFps * 0.9f + instantFps * 0.1f;
            }
        }
    }
    lastFrameTime = time;
}
}

bool SkiaRenderer::create(const CreateInfo& info) {
    info.backendContext->fMemoryAllocator =
        skgpu::VulkanMemoryAllocators::Make(*info.backendContext, skgpu::ThreadSafe::kNo);

    fContext = GrDirectContexts::MakeVulkan(*info.backendContext);
    if (!fContext) {
        fprintf(stderr, "Failed to create GrDirectContext (Vulkan).\n");
        destroy();
        return false;
    }

    fContext->setResourceCacheLimit(kMaxGpuResourceCacheBytes);

    info.extensions->init(
        info.backendContext->fGetProc,
        info.instance,
        info.physicalDevice,
        info.instanceExtensionCount,
        info.instanceExtensionNames,
        info.deviceExtensionCount,
        info.deviceExtensionNames);

    if (!fMovies.loadFromFile("movies.json")) {
        fprintf(stderr, "Warning: could not load movies.json (run from build dir or set cwd)\n");
    }

    fPosterSlots.resize(fMovies.size());
    const SkISize initialTile = computeTileTargetSize(static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight));
    fTileTargetWidth = initialTile.width();
    fTileTargetHeight = initialTile.height();

    sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
    fTypeface = mgr->makeFromFile("assets/GoogleSans-Regular.ttf");
    if (!fTypeface) {
        fprintf(stderr, "Warning: could not load any font for text rendering\n");
    }

    if (!makeLoadingPlaceholder()) {
        fprintf(stderr, "Failed to create poster loading placeholder texture.\n");
        destroy();
        return false;
    }

    fDecodeThreadExit = false;
    fDecodeThread = std::thread(&SkiaRenderer::decodeThreadMain, this);

    fTitleFont = SkFont(fTypeface, kTitleFontSize);
    fDetailTitleFont = SkFont(fTypeface, kDetailTitleFontSize);
    fDetailBodyFont = SkFont(fTypeface, kDetailBodyFontSize);
    fDetailMetaFont = SkFont(fTypeface, kDetailMetaFontSize);

    fTitlePaint.setAntiAlias(true);
    fTitlePaint.setColor(SK_ColorWHITE);

    fSelectionPaint.setStyle(SkPaint::kStroke_Style);
    fSelectionPaint.setColor(SK_ColorWHITE);
    fSelectionPaint.setStrokeWidth(kSelectionStrokeWidth);
    fSelectionPaint.setAntiAlias(true);

    fMatrix.setScale(0.8f, 0.8f, 0.8f, 1.0f);
    fDimPaint.setColorFilter(SkColorFilters::Matrix(fMatrix));

    fDetailTextPaint.setAntiAlias(true);
    fDetailTextPaint.setColor(SK_ColorWHITE);
    fFpsPaint.setAntiAlias(true);
    fFpsPaint.setColor(SK_ColorWHITE);

    return true;
}

void SkiaRenderer::destroy() {
    fDecodeThreadExit = true;
    fDecodeCv.notify_all();
    if (fDecodeThread.joinable()) {
        fDecodeThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(fDecodeMutex);
        fPendingJobs.clear();
        fCompletedDecodes.clear();
    }

    fTitleCache.clear();
    fCachedCellW = 0.0f;
    fCachedUiScale = -1.0f;
    fLayoutWidth = 0;
    fLayoutHeight = 0;
    fTileTargetWidth = 0;
    fTileTargetHeight = 0;
    fLastFrameTime = -1.0f;
    fSmoothedFps = 0.0f;
    fPosterSlots.clear();
    fPosterPlaceholder.reset();
    invalidateBackgroundBlurCache();
    fContext.reset();
}

void SkiaRenderer::invalidateBackgroundBlurCache() {
    fBlurredCurrentBackground.reset();
    fBlurredPreviousBackground.reset();
    fBlurredCurrentIndex = -1;
    fBlurredPreviousIndex = -1;
    fBlurredCurrentGeneration = 0;
    fBlurredPreviousGeneration = 0;
    fBlurredCurrentSourceImageId = 0;
    fBlurredPreviousSourceImageId = 0;
    fBlurCacheWidth = 0;
    fBlurCacheHeight = 0;
}

void SkiaRenderer::invalidateBackgroundBlurCacheForIndex(int movieIndex) {
    if (fBlurredCurrentIndex == movieIndex) {
        fBlurredCurrentBackground.reset();
        fBlurredCurrentIndex = -1;
        fBlurredCurrentGeneration = 0;
        fBlurredCurrentSourceImageId = 0;
    }
    if (fBlurredPreviousIndex == movieIndex) {
        fBlurredPreviousBackground.reset();
        fBlurredPreviousIndex = -1;
        fBlurredPreviousGeneration = 0;
        fBlurredPreviousSourceImageId = 0;
    }
}

uint32_t SkiaRenderer::backgroundGenerationForIndex(int movieIndex) const {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fPosterSlots.size())) {
        return 0;
    }
    return fPosterSlots[movieIndex].fLoadGeneration;
}

sk_sp<SkImage> SkiaRenderer::buildBlurredBackground(const sk_sp<SkImage>& source, int width, int height) const {
    if (!fContext || !source || width <= 0 || height <= 0) {
        return nullptr;
    }

    const float imgW = static_cast<float>(source->width());
    const float imgH = static_cast<float>(source->height());
    if (imgW <= 0.0f || imgH <= 0.0f) {
        return nullptr;
    }

    const SkImageInfo bgInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> backgroundSurface = SkSurfaces::RenderTarget(fContext.get(), skgpu::Budgeted::kNo, bgInfo);
    if (!backgroundSurface) {
        return nullptr;
    }

    const float scale = std::max(static_cast<float>(width) / imgW, static_cast<float>(height) / imgH);
    const float dstW = imgW * scale;
    const float dstH = imgH * scale;
    const float dstX = (static_cast<float>(width) - dstW) * 0.5f;
    const float dstY = (static_cast<float>(height) - dstH) * 0.5f;
    const SkRect backgroundRect = SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height));
    const SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);

    SkCanvas* backgroundCanvas = backgroundSurface->getCanvas();
    backgroundCanvas->clear(SK_ColorBLACK);
    backgroundCanvas->drawImageRect(source, dstRect, backgroundSampling());

    sk_sp<SkImage> composed = backgroundSurface->makeImageSnapshot();
    if (!composed) {
        return nullptr;
    }

    return fBackgroundBlurFilter.generate(fContext.get(), kBackgroundBlurRadius, composed, backgroundRect);
}

sk_sp<SkImage> SkiaRenderer::ensureBlurredBackgroundCached(bool previousSlot, int movieIndex,
                                                           int width, int height) {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fMovies.size())) {
        if (previousSlot) {
            fBlurredPreviousBackground.reset();
            fBlurredPreviousIndex = -1;
            fBlurredPreviousGeneration = 0;
            fBlurredPreviousSourceImageId = 0;
        } else {
            fBlurredCurrentBackground.reset();
            fBlurredCurrentIndex = -1;
            fBlurredCurrentGeneration = 0;
            fBlurredCurrentSourceImageId = 0;
        }
        return nullptr;
    }

    if (width != fBlurCacheWidth || height != fBlurCacheHeight) {
        invalidateBackgroundBlurCache();
        fBlurCacheWidth = width;
        fBlurCacheHeight = height;
    }

    sk_sp<SkImage>& cachedImage = previousSlot ? fBlurredPreviousBackground : fBlurredCurrentBackground;
    int& cachedIndex = previousSlot ? fBlurredPreviousIndex : fBlurredCurrentIndex;
    uint32_t& cachedGeneration = previousSlot ? fBlurredPreviousGeneration : fBlurredCurrentGeneration;
    uint32_t& cachedSourceImageId = previousSlot ? fBlurredPreviousSourceImageId : fBlurredCurrentSourceImageId;

    const uint32_t generation = backgroundGenerationForIndex(movieIndex);
    const sk_sp<SkImage> source = posterForBackground(movieIndex);
    const uint32_t sourceImageId = source ? source->uniqueID() : 0;
    if (cachedImage && cachedIndex == movieIndex && cachedGeneration == generation &&
        cachedSourceImageId == sourceImageId) {
        return cachedImage;
    }

    cachedImage = buildBlurredBackground(source, width, height);
    cachedIndex = movieIndex;
    cachedGeneration = generation;
    cachedSourceImageId = sourceImageId;
    return cachedImage;
}

void SkiaRenderer::finishScroll() {
    fScrollProgress = 1.0f;
    fScrollOffset = fTargetOffset;
    fIsScrolling = false;
    const int visibleRows = kGridRows;
    const size_t itemCount = fMovies.size();
    if (itemCount == 0) {
        clearInputQueue();
        return;
    }
    const int totalRows =
        static_cast<int>(itemCount / kGridCols + (itemCount % kGridCols != 0));
    const int maxOffset = std::max(0, totalRows - visibleRows);
    const int focusRow = fFocusIndex / kGridCols;
    if (focusRow < fScrollOffset) {
        fScrollOffset = focusRow;
    } else if (focusRow >= fScrollOffset + visibleRows) {
        fScrollOffset = focusRow - visibleRows + 1;
    }
    if (fScrollOffset > maxOffset) {
        fScrollOffset = maxOffset;
    }
    if (fScrollOffset < 0) {
        fScrollOffset = 0;
    }
    clearInputQueue();
}

float SkiaRenderer::layoutScale(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 1.0f;
    }
    return std::min(static_cast<float>(width) / kDesignWidth,
                    static_cast<float>(height) / kDesignHeight);
}

void SkiaRenderer::draw(SkCanvas* canvas, int width, int height, float time) {
    updateSmoothedFps(time, fLastFrameTime, fSmoothedFps);
    updateTileTargetSize(width, height);
    double now = platform::nowSeconds();
    updatePosterCache(now);

    if (fDetailMode) {
        drawDetailView(canvas, width, height);
        drawFpsOverlay(canvas, layoutScale(width, height));
        return;
    }

    const float uiScale = layoutScale(width, height);
    if (fTypeface) {
        fTitleFont.setSize(kTitleFontSize * uiScale);
    }
    fSelectionPaint.setStrokeWidth(kSelectionStrokeWidth * uiScale);

    if (fIsScrolling) {
        float scrollTime = time - fScrollStartTime;
        if (scrollTime >= kScrollDuration) {
            finishScroll();
        } else {
            fScrollProgress = scrollTime / kScrollDuration;
        }
    }
    if (fBackgroundIndex < 0 || fBackgroundIndex >= static_cast<int>(fMovies.size())) {
        fBackgroundIndex = std::max(0, std::min(fFocusIndex, static_cast<int>(fMovies.size()) - 1));
    }
    if (fBackgroundFading) {
        float fadeTime = time - fBackgroundFadeStartTime;
        if (fadeTime >= kBackgroundFadeDuration) {
            fBackgroundFadeProgress = 1.0f;
            fBackgroundFading = false;
            fBackgroundPrevIndex = -1;
        } else {
            fBackgroundFadeProgress = fadeTime / kBackgroundFadeDuration;
        }
    }
    canvas->clear(SK_ColorBLACK);

    const float pad = kPadding * uiScale;
    const float titleSpace = kTitleSpace * uiScale;
    const float cellH = (height - pad * (kGridRows + 1) - titleSpace * kGridRows) / kGridRows;
    float scrollY = 0.0f;
    if (fIsScrolling) {
        float t = easeInOut(fScrollProgress);
        scrollY = t * (height / kGridRows) * (fScrollingDown ? 1.0f : -1.0f);
    }
    const float cellW = (width - pad * (kGridCols + 1)) / kGridCols;

    if (cellW != fCachedCellW || uiScale != fCachedUiScale) {
        rebuildTitleCache(cellW);
        fCachedUiScale = uiScale;
    }

    auto drawBackgroundPoster = [&](SkCanvas* targetCanvas, int index, float alpha) {
        if (index < 0 || index >= static_cast<int>(fMovies.size()) || alpha <= 0.0f) {
            return;
        }
        sk_sp<SkImage> bg = posterForBackground(index);
        if (!bg) {
            return;
        }
        const float imgW = static_cast<float>(bg->width());
        const float imgH = static_cast<float>(bg->height());
        if (imgW <= 0.0f || imgH <= 0.0f) {
            return;
        }
        const float scale = std::max(static_cast<float>(width) / imgW, static_cast<float>(height) / imgH);
        const float dstW = imgW * scale;
        const float dstH = imgH * scale;
        const float dstX = (static_cast<float>(width) - dstW) * 0.5f;
        const float dstY = (static_cast<float>(height) - dstH) * 0.5f;
        SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
        SkPaint bgPaint;
        bgPaint.setAntiAlias(true);
        bgPaint.setAlphaf(std::max(0.0f, std::min(1.0f, alpha)));
        targetCanvas->drawImageRect(bg, dstRect, backgroundSampling(), &bgPaint);
    };
    const SkRect backgroundRect = SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height));
    auto drawBlurredLayer = [&](const sk_sp<SkImage>& blur, float alpha) {
        if (!blur || alpha <= 0.0f) {
            return;
        }
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setAlphaf(std::max(0.0f, std::min(1.0f, alpha)));
        const float texScaleX = static_cast<float>(blur->width()) / static_cast<float>(width);
        const float texScaleY = static_cast<float>(blur->height()) / static_cast<float>(height);
        BlurBackgroundMeshParams meshParams;
        meshParams.width = width;
        meshParams.height = height;
        meshParams.uiScale = uiScale;
        meshParams.scrollY = scrollY;
        meshParams.texScaleX = texScaleX;
        meshParams.texScaleY = texScaleY;
        meshParams.scrollOffset = fScrollOffset;
        meshParams.movieCount = static_cast<int>(fMovies.size());
        meshParams.gridCols = kGridCols;
        meshParams.gridRows = kGridRows;
        meshParams.paddingDesign = kPadding;
        meshParams.titleSpaceDesign = kTitleSpace;
        meshParams.blurHoleInsetDesign = kBlurHoleInset;
        meshParams.cornerRadiusDesign = kCornerRadius;
        sk_sp<SkVertices> mesh = makeBlurredBackgroundMesh(
            meshParams, [this](int movieIndex) { return posterForGrid(movieIndex); });
        if (!mesh) {
            canvas->drawImageRect(blur, backgroundRect, backgroundSampling(), &paint);
            return;
        }
        paint.setShader(blur->makeShader(backgroundSampling()));
        canvas->drawVertices(mesh, SkBlendMode::kSrcOver, paint);
    };

    if (fBackgroundFading) {
        float fadeT = easeInOut(fBackgroundFadeProgress);
        sk_sp<SkImage> previousBlur = ensureBlurredBackgroundCached(true, fBackgroundPrevIndex, width, height);
        sk_sp<SkImage> currentBlur = ensureBlurredBackgroundCached(false, fBackgroundIndex, width, height);

        if (previousBlur) {
            drawBlurredLayer(previousBlur, 1.0f - fadeT);
        } else {
            drawBackgroundPoster(canvas, fBackgroundPrevIndex, 1.0f - fadeT);
        }

        if (currentBlur) {
            drawBlurredLayer(currentBlur, fadeT);
        } else {
            drawBackgroundPoster(canvas, fBackgroundIndex, fadeT);
        }
    } else {
        sk_sp<SkImage> currentBlur = ensureBlurredBackgroundCached(false, fBackgroundIndex, width, height);
        if (currentBlur) {
            drawBlurredLayer(currentBlur, 1.0f);
        } else {
            drawBackgroundPoster(canvas, fBackgroundIndex, 1.0f);
        }
        if (fBackgroundPrevIndex >= 0) {
            invalidateBackgroundBlurCacheForIndex(fBackgroundPrevIndex);
            fBackgroundPrevIndex = -1;
        }
    }
    // Multiply blend gives a predictable dim regardless of surface alpha handling.
    const float dimFactor = 1.0f - (static_cast<float>(kBackgroundDimAlpha) / 255.0f);
    const uint8_t mul = static_cast<uint8_t>(
        std::round(std::max(0.0f, std::min(1.0f, dimFactor)) * 255.0f));
    SkPaint backgroundDimPaint;
    backgroundDimPaint.setBlendMode(SkBlendMode::kMultiply);
    backgroundDimPaint.setColor(SkColorSetRGB(mul, mul, mul));
    canvas->drawRect(SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
        backgroundDimPaint);

    if (fSkipGridForeground) {
        drawFpsOverlay(canvas, uiScale);
        return;
    }

    const float cornerR = kCornerRadius * uiScale;
    const float selOffset = kSelectionOffset * uiScale;

    const int focusRow = fFocusIndex / kGridCols;
    for (int row = 0; row < kGridRows; ++row) {
        bool rowHasHighlight = (fScrollOffset + row) == focusRow;

        for (int col = 0; col < kGridCols; ++col) {
            int idx = (fScrollOffset + row) * kGridCols + col;
            if (idx >= static_cast<int>(fMovies.size())) {
                continue;
            }
            sk_sp<SkImage> img = posterForGrid(idx);
            if (!img) {
                continue;
            }

            float cellX = pad + col * (cellW + pad);
            float cellY = pad + row * (cellH + pad + titleSpace) - scrollY;

            float imgW = static_cast<float>(img->width());
            float imgH = static_cast<float>(img->height());
            float scale = std::min(cellW / imgW, cellH / imgH);
            float dstW = imgW * scale;
            float dstH = imgH * scale;
            float dstX = cellX + (cellW - dstW) * 0.5f;
            float dstY = cellY + (cellH - dstH) * 0.5f;

            SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
            SkRRect rrect = SkRRect::MakeRectXY(dstRect, cornerR, cornerR);

            canvas->save();
            canvas->clipRRect(rrect, true);
            canvas->drawImageRect(img, dstRect, gridSampling(), rowHasHighlight ? nullptr : &fDimPaint);
            canvas->restore();

            if (idx == fFocusIndex) {
                SkRect selRect = dstRect.makeOutset(selOffset, selOffset);
                SkRRect selRRect = SkRRect::MakeRectXY(selRect,
                    cornerR + selOffset, cornerR + selOffset);
                canvas->drawRRect(selRRect, fSelectionPaint);
            }

            if (idx < static_cast<int>(fTitleCache.size()) && fTitleCache[idx].blob) {
                float titleX = cellX + (cellW - fTitleCache[idx].width) * 0.5f;
                float titleY = cellY + cellH + titleSpace * 0.75f;

                if (idx == fFocusIndex) {
                    float maxWidth = cellW * 0.9f;
                    float textY = titleY;
                    drawScrollingText(canvas, fTitleCache[idx].fullText, titleX, textY, maxWidth, fTitlePaint,
                                      time, uiScale);
                } else {
                    canvas->drawTextBlob(fTitleCache[idx].blob, titleX, titleY, fTitlePaint);
                }
            }
        }
    }
    drawFpsOverlay(canvas, uiScale);
}

void SkiaRenderer::drawFpsOverlay(SkCanvas* canvas, float uiScale) {
    if (!fShowFps || fSmoothedFps <= 0.0f) {
        return;
    }
    const float fontSize = std::max(14.0f, 20.0f * uiScale);
    SkFont fpsFont(fTypeface, fontSize);
    fpsFont.setEdging(SkFont::Edging::kAntiAlias);
    fpsFont.setSubpixel(false);

    char fpsText[64];
    std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", fSmoothedFps);
    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromText(
        fpsText, std::strlen(fpsText), fpsFont, SkTextEncoding::kUTF8);
    if (!blob) {
        return;
    }

    SkFontMetrics fm;
    fpsFont.getMetrics(&fm);
    const float x = std::max(8.0f, 12.0f * uiScale);
    const float y = std::max(8.0f, 12.0f * uiScale) - fm.fAscent;

    canvas->drawTextBlob(blob, x, y, fFpsPaint);
}

bool SkiaRenderer::flushAndSubmit(SkSurface* surface, VkSemaphore signalSemaphore, uint32_t presentQueueIndex) {
    GrBackendSemaphore beSignal = GrBackendSemaphores::MakeVk(signalSemaphore);
    GrFlushInfo flushInfo;
    flushInfo.fNumSemaphores = 1;
    flushInfo.fSignalSemaphores = &beSignal;
    skgpu::MutableTextureState presentState = skgpu::MutableTextureStates::MakeVulkan(
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, presentQueueIndex);
    fContext->flush(surface, flushInfo, &presentState);
    fContext->submit();
    return true;
}

float SkiaRenderer::easeInOut(float t) const {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(2.0f * (1.0f - t), 3.0f) / 2.0f;
}

void SkiaRenderer::rebuildTitleCache(float cellW) {
    fCachedCellW = cellW;
    fTitleCache.resize(fMovies.size());
    float maxWidth = cellW * 0.9f;

    for (size_t i = 0; i < fMovies.size(); ++i) {
        const std::string& fullText = fMovies[i].title;
        std::string display = ellipsizeText(fullText, maxWidth, fTitleFont);
        float w = fTitleFont.measureText(display.c_str(), display.size(), SkTextEncoding::kUTF8);
        fTitleCache[i].text = std::move(display);
        fTitleCache[i].fullText = fullText;
        fTitleCache[i].width = w;
        fTitleCache[i].textWidth = fTitleFont.measureText(fullText.c_str(), fullText.size(), SkTextEncoding::kUTF8);
        fTitleCache[i].blob = SkTextBlob::MakeFromText(
            fTitleCache[i].text.c_str(), fTitleCache[i].text.size(),
            fTitleFont, SkTextEncoding::kUTF8);
    }
}

std::string SkiaRenderer::ellipsizeText(const std::string& text, float maxWidth, SkFont& font) {
    if (text.empty()) return text;

    float textWidth = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);
    if (textWidth <= maxWidth) {
        return text;
    }

    static const std::string ellipsis = "...";
    float ellipsisWidth = font.measureText(ellipsis.c_str(), ellipsis.size(), SkTextEncoding::kUTF8);
    if (ellipsisWidth >= maxWidth) {
        return "";
    }

    float availableWidth = maxWidth - ellipsisWidth;

    size_t lo = 0, hi = text.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        float w = font.measureText(text.c_str(), mid, SkTextEncoding::kUTF8);
        if (w <= availableWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return text.substr(0, lo) + ellipsis;
}

void SkiaRenderer::enqueueInputEvent(int key, bool pressed) {
    std::lock_guard<std::mutex> lock(fInputMutex);
    fInputQueue.push(std::make_pair(key, pressed));
}

bool SkiaRenderer::pollInputEvent(std::pair<int, bool>& event) {
    std::lock_guard<std::mutex> lock(fInputMutex);
    if (fInputQueue.empty()) {
        return false;
    }
    event = fInputQueue.front();
    fInputQueue.pop();
    return true;
}

void SkiaRenderer::clearInputQueue() {
    std::lock_guard<std::mutex> lock(fInputMutex);
    while (!fInputQueue.empty()) {
        fInputQueue.pop();
    }
}

void SkiaRenderer::processInputEvent(int key, bool pressed) {
    if (!pressed) return;

    if (fDetailMode) {
        if (key == platform::kKeyEscape) {
            fDetailMode = false;
            clearInputQueue();
        }
        return;
    }

    if (fIsScrolling) {
        return;
    }

    const int itemCount = static_cast<int>(fMovies.size());
    if (itemCount <= 0) {
        return;
    }

    const int maxIndex = itemCount - 1;
    const int visibleRows = kGridRows;
    const int totalRows =
        static_cast<int>(static_cast<size_t>(itemCount) / kGridCols + (itemCount % kGridCols != 0));
    const int maxOffset = std::max(0, totalRows - visibleRows);

    if (key == platform::kKeyEnter || key == platform::kKeyKpEnter) {
        if (fFocusIndex >= 0 && fFocusIndex < itemCount) {
            fDetailMode = true;
            fDetailIndex = fFocusIndex;
        }
        return;
    }

    int previousFocus = fFocusIndex;
    int focusRow = fFocusIndex / kGridCols;
    int focusCol = fFocusIndex % kGridCols;
    bool changed = false;

    switch (key) {
        case platform::kKeyUp:
            if (focusRow > 0) {
                fFocusIndex -= kGridCols;
                changed = true;
                focusRow = fFocusIndex / kGridCols;
                if (focusRow < fScrollOffset && fScrollOffset > 0) {
                    fTargetOffset = fScrollOffset - 1;
                    fIsScrolling = true;
                    fScrollProgress = 0.0f;
                    fScrollStartTime = static_cast<float>(platform::nowSeconds());
                    fScrollingDown = false;
                }
            }
            break;
        case platform::kKeyDown:
            if (focusRow < totalRows - 1) {
                int nextIdx = (focusRow + 1) * kGridCols + focusCol;
                if (nextIdx > maxIndex) {
                    nextIdx = maxIndex;
                }
                if (nextIdx != fFocusIndex) {
                    fFocusIndex = nextIdx;
                    changed = true;
                }
                focusRow = fFocusIndex / kGridCols;
                if (focusRow >= fScrollOffset + visibleRows && fScrollOffset < maxOffset) {
                    fTargetOffset = fScrollOffset + 1;
                    fIsScrolling = true;
                    fScrollProgress = 0.0f;
                    fScrollStartTime = static_cast<float>(platform::nowSeconds());
                    fScrollingDown = true;
                }
            }
            break;
        case platform::kKeyLeft:
            if (focusCol > 0) {
                fFocusIndex--;
                changed = true;
            }
            break;
        case platform::kKeyRight:
            if (focusCol < kGridCols - 1 && fFocusIndex < maxIndex) {
                fFocusIndex++;
                changed = true;
            }
            break;
    }

    if (changed) {
        fFocusIndex = std::max(0, std::min(fFocusIndex, maxIndex));
        fIsTextScrolling = true;
        float now = static_cast<float>(platform::nowSeconds());
        fScrollingTextStartTime = now;
        if (fFocusIndex != previousFocus) {
            if (fBackgroundIndex < 0 || fBackgroundIndex >= itemCount) {
                fBackgroundIndex = previousFocus;
            }
            fBackgroundPrevIndex = fBackgroundIndex;
            fBackgroundIndex = fFocusIndex;
            fBackgroundFadeStartTime = now;
            fBackgroundFadeProgress = 0.0f;
            fBackgroundFading = (fBackgroundPrevIndex != fBackgroundIndex);
            if (!fBackgroundFading) {
                fBackgroundPrevIndex = -1;
                fBackgroundFadeProgress = 1.0f;
            }
        }
    }

    if (fIsScrolling) {
        clearInputQueue();
    }
}

void SkiaRenderer::drawScrollingText(SkCanvas* canvas, const std::string& text, float x, float y, float maxWidth,
                                     SkPaint& paint, float time, float uiScale) {
    if (text.empty()) return;

    if (fTitleCache.empty()) return;

    size_t idx = static_cast<size_t>(fFocusIndex);
    if (idx >= fTitleCache.size()) return;

    float textWidth = fTitleCache[idx].textWidth;

    if (textWidth <= maxWidth) {
        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromText(text.c_str(), text.size(), fTitleFont, SkTextEncoding::kUTF8);
        canvas->drawTextBlob(blob, x, y, paint);
        return;
    }

    const float scrollSpeed = kTextScrollSpeed * uiScale;
    float scrollDuration = textWidth / scrollSpeed;
    float pauseTime = scrollDuration + kTextScrollPauseDuration;
    float scrollTime = time - fScrollingTextStartTime;

    if (scrollTime >= pauseTime) {
        fScrollingTextStartTime = time;
        scrollTime = 0.0f;
    }

    float scrollOffset = 0.0f;
    if (scrollTime < scrollDuration) {
        scrollOffset = scrollTime * scrollSpeed;
    } else if (scrollTime < pauseTime) {
        scrollOffset = scrollDuration * scrollSpeed;
    }

    float currentX = (x + maxWidth) - scrollOffset;

    canvas->save();
    const float clipAscent = kScrollingTextClipAscent * uiScale;
    const float clipHeight = kScrollingTextClipHeight * uiScale;
    SkRect clipRect = SkRect::MakeXYWH(x, y - clipAscent, maxWidth, clipHeight);
    canvas->clipRect(clipRect);

    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromText(text.c_str(), text.size(), fTitleFont, SkTextEncoding::kUTF8);
    canvas->drawTextBlob(blob, currentX, y, paint);

    canvas->restore();
}
