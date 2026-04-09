#include "SkiaRenderer.hpp"
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_empty.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
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
#include <sstream>
#include <GLFW/glfw3.h>

bool SkiaRenderer::makeLoadingPlaceholder() {
    static const char kLoadingLabel[] = "Loading";

    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(SkImageInfo::Make(
            kLoadingPlaceholderWidth,
            kLoadingPlaceholderHeight,
            kRGBA_8888_SkColorType,
            kPremul_SkAlphaType))) {
        return false;
    }
    bitmap.eraseColor(SK_ColorBLACK);

    if (fTypeface) {
        SkFont font(fTypeface, kLoadingPlaceholderFontSize);
        font.setEdging(SkFont::Edging::kAntiAlias);
        font.setSubpixel(false);

        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromText(
            kLoadingLabel, std::strlen(kLoadingLabel), font, SkTextEncoding::kUTF8);
        if (blob) {
            float textW =
                font.measureText(kLoadingLabel, std::strlen(kLoadingLabel), SkTextEncoding::kUTF8);
            SkFontMetrics fm;
            font.getMetrics(&fm);
            float x = (static_cast<float>(kLoadingPlaceholderWidth) - textW) * 0.5f;
            float baselineY = static_cast<float>(kLoadingPlaceholderHeight) * 0.5f -
                (fm.fAscent + fm.fDescent) * 0.5f;

            SkCanvas canvas(bitmap);
            SkPaint paint;
            paint.setAntiAlias(true);
            paint.setColor(SK_ColorWHITE);
            canvas.drawTextBlob(blob, x, baselineY, paint);
        }
    }

    bitmap.setImmutable();
    sk_sp<SkImage> raster = SkImages::RasterFromBitmap(bitmap);
    if (!raster) {
        return false;
    }
    sk_sp<SkImage> gpu = SkImages::TextureFromImage(fContext.get(), raster.get());
    fPosterPlaceholder = gpu ? std::move(gpu) : std::move(raster);
    return static_cast<bool>(fPosterPlaceholder);
}

void SkiaRenderer::decodeThreadMain() {
    for (;;) {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(fDecodeMutex);
            fDecodeCv.wait(lock, [&] { return fDecodeThreadExit.load() || !fPendingJobs.empty(); });
            if (fPendingJobs.empty()) {
                if (fDecodeThreadExit.load()) {
                    return;
                }
                continue;
            }
            job = fPendingJobs.front();
            fPendingJobs.pop_front();
        }

        std::unique_ptr<SkBitmap> bitmap;
        bool ok = false;
        if (job.fMovieIndex >= 0 && job.fMovieIndex < static_cast<int>(fMovies.size())) {
            std::string path = "assets/" + fMovies[job.fMovieIndex].filename;
            sk_sp<SkData> data = SkData::MakeFromFileName(path.c_str());
            if (data) {
                std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(data);
                if (codec) {
                    bitmap = std::make_unique<SkBitmap>();
                    if (bitmap->tryAllocPixels(codec->getInfo())) {
                        if (codec->getPixels(bitmap->info(), bitmap->getPixels(), bitmap->rowBytes()) ==
                            SkCodec::kSuccess) {
                            bitmap->setImmutable();
                            ok = true;
                        }
                    }
                }
            }
        }

        DecodedCompletion completion;
        completion.fMovieIndex = job.fMovieIndex;
        completion.fGeneration = job.fGeneration;
        completion.fSuccess = ok && bitmap != nullptr;
        if (completion.fSuccess) {
            completion.fBitmap = std::move(bitmap);
        }

        {
            std::lock_guard<std::mutex> lock(fDecodeMutex);
            fCompletedDecodes.push_back(std::move(completion));
        }
    }
}

void SkiaRenderer::enqueueDecodeJob(int movieIndex, uint32_t generation) {
    std::lock_guard<std::mutex> lock(fDecodeMutex);
    for (auto it = fPendingJobs.begin(); it != fPendingJobs.end();) {
        if (it->fMovieIndex == movieIndex) {
            it = fPendingJobs.erase(it);
        } else {
            ++it;
        }
    }
    fPendingJobs.push_back(DecodeJob{movieIndex, generation});
    trimDecodeJobQueueLocked();
    fDecodeCv.notify_one();
}

void SkiaRenderer::trimDecodeJobQueueLocked() {
    while (fPendingJobs.size() > kMaxPendingDecodeJobs) {
        DecodeJob dropped = fPendingJobs.front();
        fPendingJobs.pop_front();
        if (dropped.fMovieIndex >= 0 &&
            dropped.fMovieIndex < static_cast<int>(fPosterSlots.size())) {
            PosterSlot& slot = fPosterSlots[dropped.fMovieIndex];
            slot.fLoadGeneration++;
            if (!slot.fImage) {
                slot.fState = PosterState::Empty;
            }
        }
    }
}

void SkiaRenderer::requestPosterDecode(int movieIndex) {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fMovies.size())) {
        return;
    }
    PosterSlot& slot = fPosterSlots[movieIndex];
    if (slot.fState == PosterState::Ready && slot.fImage) {
        return;
    }
    if (slot.fState == PosterState::Failed) {
        return;
    }
    if (slot.fState == PosterState::Loading) {
        return;
    }
    slot.fLoadGeneration++;
    uint32_t gen = slot.fLoadGeneration;
    slot.fState = PosterState::Loading;
    enqueueDecodeJob(movieIndex, gen);
}

void SkiaRenderer::drainDecodeCompletions() {
    for (;;) {
        DecodedCompletion completion;
        {
            std::lock_guard<std::mutex> lock(fDecodeMutex);
            if (fCompletedDecodes.empty()) {
                break;
            }
            completion = std::move(fCompletedDecodes.front());
            fCompletedDecodes.pop_front();
        }

        if (completion.fMovieIndex < 0 ||
            completion.fMovieIndex >= static_cast<int>(fPosterSlots.size())) {
            continue;
        }
        PosterSlot& slot = fPosterSlots[completion.fMovieIndex];
        if (completion.fGeneration != slot.fLoadGeneration) {
            continue;
        }
        if (!completion.fSuccess || !completion.fBitmap) {
            slot.fState = PosterState::Failed;
            slot.fImage.reset();
            continue;
        }

        sk_sp<SkImage> raster = SkImages::RasterFromBitmap(*completion.fBitmap);
        if (!raster) {
            slot.fState = PosterState::Failed;
            slot.fImage.reset();
            continue;
        }

        sk_sp<SkImage> gpu = SkImages::TextureFromImage(fContext.get(), raster.get());
        slot.fImage = gpu ? std::move(gpu) : std::move(raster);
        slot.fState = PosterState::Ready;
    }
}

void SkiaRenderer::evictPosterCache(double now, const std::vector<int>&) {
    bool evictedAny = false;
    std::vector<int> resident;
    resident.reserve(fPosterSlots.size());
    for (int i = 0; i < static_cast<int>(fPosterSlots.size()); ++i) {
        PosterSlot& slot = fPosterSlots[i];
        if (slot.fState == PosterState::Ready && slot.fImage) {
            if (now - slot.fLastUsed > kPosterEvictIdleSeconds) {
                slot.fImage.reset();
                slot.fState = PosterState::Empty;
                slot.fLoadGeneration++;
                evictedAny = true;
            } else {
                resident.push_back(i);
            }
        }
    }

    while (resident.size() > kMaxResidentPosters) {
        int oldestIdx = resident[0];
        double oldestT = fPosterSlots[oldestIdx].fLastUsed;
        for (int idx : resident) {
            if (fPosterSlots[idx].fLastUsed < oldestT) {
                oldestT = fPosterSlots[idx].fLastUsed;
                oldestIdx = idx;
            }
        }
        PosterSlot& slot = fPosterSlots[oldestIdx];
        slot.fImage.reset();
        slot.fState = PosterState::Empty;
        slot.fLoadGeneration++;
        evictedAny = true;
        resident.erase(std::find(resident.begin(), resident.end(), oldestIdx));
    }

    if (evictedAny && fContext) {
        fContext->purgeUnlockedResources(GrPurgeResourceOptions::kAllResources);
    }
}

void SkiaRenderer::updatePosterCache(double now) {
    drainDecodeCompletions();

    std::vector<int> touched;
    touched.reserve(static_cast<size_t>(kGridCols * kGridRows) + 1u);
    if (fDetailMode) {
        if (fDetailIndex >= 0 && fDetailIndex < static_cast<int>(fMovies.size())) {
            touched.push_back(fDetailIndex);
        }
    } else {
        for (int row = 0; row < kGridRows; ++row) {
            for (int col = 0; col < kGridCols; ++col) {
                int idx = (fScrollOffset + row) * kGridCols + col;
                if (idx < static_cast<int>(fMovies.size())) {
                    touched.push_back(idx);
                }
            }
        }
    }

    for (int idx : touched) {
        fPosterSlots[idx].fLastUsed = now;
        requestPosterDecode(idx);
    }

    evictPosterCache(now, touched);
}

sk_sp<SkImage> SkiaRenderer::posterForIndex(int movieIndex) const {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fPosterSlots.size())) {
        return fPosterPlaceholder;
    }
    const PosterSlot& slot = fPosterSlots[movieIndex];
    if (slot.fState == PosterState::Ready && slot.fImage) {
        return slot.fImage;
    }
    return fPosterPlaceholder;
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

    sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
    fTypeface = mgr->makeFromFile("assets/Roboto-Regular.ttf");
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
    fSelectionPaint.setStrokeWidth(3.0f);
    fSelectionPaint.setAntiAlias(true);

    fMatrix.setScale(0.8f, 0.8f, 0.8f, 1.0f);
    fDimPaint.setColorFilter(SkColorFilters::Matrix(fMatrix));

    fDetailTextPaint.setAntiAlias(true);
    fDetailTextPaint.setColor(SK_ColorWHITE);

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
    fPosterSlots.clear();
    fPosterPlaceholder.reset();
    fContext.reset();
}

void SkiaRenderer::finishScroll() {
    fScrollProgress = 1.0f;
    fScrollOffset = fTargetOffset;
    fIsScrolling = false;
    const int visibleRows = kGridRows;
    const size_t itemCount = fMovies.size();
    if (itemCount == 0) {
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
}

void SkiaRenderer::draw(SkCanvas* canvas, int width, int height, float time) {
    double now = glfwGetTime();
    updatePosterCache(now);

    if (fDetailMode) {
        drawDetailView(canvas, width, height);
        return;
    }

    if (fIsScrolling) {
        float scrollTime = time - fScrollStartTime;
        if (scrollTime >= kScrollDuration) {
            finishScroll();
        } else {
            fScrollProgress = scrollTime / kScrollDuration;
        }
    }

    float scrollY = 0.0f;
    if (fIsScrolling) {
        float t = easeInOut(fScrollProgress);
        scrollY = t * (height / kGridRows) * (fScrollingDown ? 1.0f : -1.0f);
    }

    canvas->clear(SK_ColorGRAY);

    const float cellW = (width - kPadding * (kGridCols + 1)) / kGridCols;
    const float cellH = (height - kPadding * (kGridRows + 1) - kTitleSpace * kGridRows) / kGridRows;

    if (cellW != fCachedCellW) {
        rebuildTitleCache(cellW);
    }

    const int focusRow = fFocusIndex / kGridCols;

    for (int row = 0; row < kGridRows; ++row) {
        bool rowHasHighlight = (fScrollOffset + row) == focusRow;

        for (int col = 0; col < kGridCols; ++col) {
            int idx = (fScrollOffset + row) * kGridCols + col;
            if (idx >= static_cast<int>(fMovies.size())) {
                continue;
            }
            sk_sp<SkImage> img = posterForIndex(idx);
            if (!img) {
                continue;
            }

            float cellX = kPadding + col * (cellW + kPadding);
            float cellY = kPadding + row * (cellH + kPadding + kTitleSpace) - scrollY;

            float imgW = static_cast<float>(img->width());
            float imgH = static_cast<float>(img->height());
            float scale = std::min(cellW / imgW, cellH / imgH);
            float dstW = imgW * scale;
            float dstH = imgH * scale;
            float dstX = cellX + (cellW - dstW) * 0.5f;
            float dstY = cellY + (cellH - dstH) * 0.5f;

            SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
            SkRRect rrect = SkRRect::MakeRectXY(dstRect, kCornerRadius, kCornerRadius);

            canvas->save();
            canvas->clipRRect(rrect, true);
            canvas->drawImageRect(img, dstRect, SkSamplingOptions(), rowHasHighlight ? nullptr : &fDimPaint);
            canvas->restore();

            if (idx == fFocusIndex) {
                SkRect selRect = dstRect.makeOutset(kSelectionOffset, kSelectionOffset);
                SkRRect selRRect = SkRRect::MakeRectXY(selRect,
                    kCornerRadius + kSelectionOffset, kCornerRadius + kSelectionOffset);
                canvas->drawRRect(selRRect, fSelectionPaint);
            }

            if (idx < static_cast<int>(fTitleCache.size()) && fTitleCache[idx].blob) {
                float titleX = cellX + (cellW - fTitleCache[idx].width) * 0.5f;
                float titleY = cellY + cellH + kTitleSpace * 0.75f;

                if (idx == fFocusIndex) {
                    float maxWidth = cellW * 0.9f;
                    float textY = titleY;
                    drawScrollingText(canvas, fTitleCache[idx].fullText, titleX, textY, maxWidth, fTitlePaint, time);
                } else {
                    canvas->drawTextBlob(fTitleCache[idx].blob, titleX, titleY, fTitlePaint);
                }
            }
        }
    }
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
        if (key == GLFW_KEY_ESCAPE) {
            fDetailMode = false;
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

    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (fFocusIndex >= 0 && fFocusIndex < itemCount) {
            fDetailMode = true;
            fDetailIndex = fFocusIndex;
        }
        return;
    }

    int focusRow = fFocusIndex / kGridCols;
    int focusCol = fFocusIndex % kGridCols;
    bool changed = false;

    switch (key) {
        case GLFW_KEY_UP:
            if (focusRow > 0) {
                fFocusIndex -= kGridCols;
                changed = true;
                focusRow = fFocusIndex / kGridCols;
                if (focusRow < fScrollOffset && fScrollOffset > 0) {
                    fTargetOffset = fScrollOffset - 1;
                    fIsScrolling = true;
                    fScrollProgress = 0.0f;
                    fScrollStartTime = static_cast<float>(glfwGetTime());
                    fScrollingDown = false;
                }
            }
            break;
        case GLFW_KEY_DOWN:
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
                    fScrollStartTime = static_cast<float>(glfwGetTime());
                    fScrollingDown = true;
                }
            }
            break;
        case GLFW_KEY_LEFT:
            if (focusCol > 0) {
                fFocusIndex--;
                changed = true;
            }
            break;
        case GLFW_KEY_RIGHT:
            if (focusCol < kGridCols - 1 && fFocusIndex < maxIndex) {
                fFocusIndex++;
                changed = true;
            }
            break;
    }

    if (changed) {
        fFocusIndex = std::max(0, std::min(fFocusIndex, maxIndex));
        fIsTextScrolling = true;
        fScrollingTextStartTime = glfwGetTime();
    }

    if (fIsScrolling) {
        clearInputQueue();
    }
}

void SkiaRenderer::drawScrollingText(SkCanvas* canvas, const std::string& text, float x, float y, float maxWidth, SkPaint& paint, float time) {
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

    float scrollDuration = textWidth / kTextScrollSpeed;
    float pauseTime = scrollDuration + kTextScrollPauseDuration;
    float scrollTime = time - fScrollingTextStartTime;

    if (scrollTime >= pauseTime) {
        fScrollingTextStartTime = time;
        scrollTime = 0.0f;
    }

    float scrollOffset = 0.0f;
    if (scrollTime < scrollDuration) {
        scrollOffset = scrollTime * kTextScrollSpeed;
    } else if (scrollTime < pauseTime) {
        scrollOffset = scrollDuration * kTextScrollSpeed;
    }

    float currentX = (x + maxWidth) - scrollOffset;

    canvas->save();
    SkRect clipRect = SkRect::MakeXYWH(x, y - 20, maxWidth, 40);
    canvas->clipRect(clipRect);

    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromText(text.c_str(), text.size(), fTitleFont, SkTextEncoding::kUTF8);
    canvas->drawTextBlob(blob, currentX, y, paint);

    canvas->restore();
}

std::string SkiaRenderer::formatDetailPrice(const Movie& movie) {
    if (movie.ppv_price == "Free") {
        return "Free";
    }
    return std::string("£") + movie.ppv_price;
}

std::string SkiaRenderer::formatDetailMetadata(const Movie& movie) {
    std::string line = formatDetailPrice(movie);
    line += " · ";
    line += std::to_string(movie.runtime);
    line += " min · RT: ";
    line += std::to_string(movie.rt_score);
    line += "%";
    return line;
}

std::vector<std::string> SkiaRenderer::wrapTextToLines(const std::string& text, float maxWidth, SkFont& font) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f) {
        return lines;
    }

    std::istringstream iss(text);
    std::string word;
    std::string currentLine;

    auto flushWordTooLong = [&](std::string& w) {
        if (font.measureText(w.c_str(), w.size(), SkTextEncoding::kUTF8) <= maxWidth) {
            return w;
        }
        return SkiaRenderer::ellipsizeText(w, maxWidth, font);
    };

    while (iss >> word) {
        word = flushWordTooLong(word);
        std::string trial = currentLine.empty() ? word : (currentLine + " " + word);
        float w = font.measureText(trial.c_str(), trial.size(), SkTextEncoding::kUTF8);
        if (w <= maxWidth || currentLine.empty()) {
            currentLine = std::move(trial);
        } else {
            lines.push_back(currentLine);
            currentLine = word;
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(std::move(currentLine));
    }
    return lines;
}

void SkiaRenderer::drawDetailView(SkCanvas* canvas, int width, int height) {
    canvas->clear(SK_ColorBLACK);

    if (fDetailIndex < 0 || fDetailIndex >= static_cast<int>(fMovies.size())) {
        return;
    }

    const Movie& movie = fMovies[fDetailIndex];
    sk_sp<SkImage> poster = posterForIndex(fDetailIndex);

    if (poster) {
        float imgW = static_cast<float>(poster->width());
        float imgH = static_cast<float>(poster->height());
        float scale = std::max(static_cast<float>(width) / imgW, static_cast<float>(height) / imgH);
        float dstW = imgW * scale;
        float dstH = imgH * scale;
        float dstX = (static_cast<float>(width) - dstW) * 0.5f;
        float dstY = (static_cast<float>(height) - dstH) * 0.5f;
        SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);

        canvas->save();
        canvas->clipRect(SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)), true);
        canvas->drawImageRect(poster, dstRect, SkSamplingOptions());
        canvas->restore();
    } else {
        canvas->drawColor(SK_ColorDKGRAY);
    }

    const float panelLeft = static_cast<float>(width) * (1.0f - kDetailPanelFraction);
    const float featherW = static_cast<float>(width) * kDetailFeatherFraction;
    const float featherLeft = panelLeft - featherW;
    const SkColor panelColor = SkColorSetA(SK_ColorBLACK, 200);

    SkPoint gradPts[2] = { {featherLeft, 0.0f}, {panelLeft, 0.0f} };
    SkColor4f gradColorStops[2] = {
        SkColor4f::FromColor(SK_ColorTRANSPARENT),
        SkColor4f::FromColor(panelColor),
    };
    SkGradient::Colors gradColors(SkSpan<const SkColor4f>(gradColorStops, 2), SkTileMode::kClamp);
    SkGradient gradSpec(gradColors, SkGradient::Interpolation{});
    sk_sp<SkShader> gradShader = SkShaders::LinearGradient(gradPts, gradSpec, nullptr);

    SkPaint featherPaint;
    featherPaint.setShader(gradShader);
    featherPaint.setAntiAlias(true);
    canvas->drawRect(SkRect::MakeLTRB(featherLeft, 0.0f, panelLeft, static_cast<float>(height)), featherPaint);

    SkPaint solidPanelPaint;
    solidPanelPaint.setColor(panelColor);
    solidPanelPaint.setAntiAlias(true);
    canvas->drawRect(SkRect::MakeLTRB(panelLeft, 0.0f, static_cast<float>(width), static_cast<float>(height)),
        solidPanelPaint);

    const float innerLeft = panelLeft + kDetailPanelPadding;
    const float innerRight = static_cast<float>(width) - kDetailPanelPadding;
    const float textMaxW = std::max(1.0f, innerRight - innerLeft);

    SkFontMetrics fmTitle;
    fDetailTitleFont.getMetrics(&fmTitle);
    SkFontMetrics fmBody;
    fDetailBodyFont.getMetrics(&fmBody);
    SkFontMetrics fmMeta;
    fDetailMetaFont.getMetrics(&fmMeta);

    const float bodyLineHeight = fmBody.fDescent - fmBody.fAscent + 4.0f;

    const float metaBaseline =
        static_cast<float>(height) - kDetailPanelPadding - fmMeta.fDescent;
    const float metaTop = metaBaseline + fmMeta.fAscent;

    std::string titleDisplay = ellipsizeText(movie.title, textMaxW, fDetailTitleFont);
    sk_sp<SkTextBlob> titleBlob =
        SkTextBlob::MakeFromText(titleDisplay.c_str(), titleDisplay.size(), fDetailTitleFont, SkTextEncoding::kUTF8);

    const float titleBaseline = kDetailPanelPadding - fmTitle.fAscent;
    canvas->drawTextBlob(titleBlob, innerLeft, titleBaseline, fDetailTextPaint);

    const float titleBottom = titleBaseline + fmTitle.fDescent;
    const float synopsisTop = titleBottom + kDetailTitleBodyGap;
    const float synopsisMaxBottom = metaTop - kDetailBodyMetaGap;
    float synopsisAvailable = synopsisMaxBottom - synopsisTop;
    if (synopsisAvailable < bodyLineHeight) {
        synopsisAvailable = bodyLineHeight;
    }

    int maxSynopsisLines = static_cast<int>(std::floor(synopsisAvailable / bodyLineHeight));
    maxSynopsisLines = std::max(1, maxSynopsisLines);

    std::vector<std::string> synopsisLines = wrapTextToLines(movie.synopsis, textMaxW, fDetailBodyFont);
    if (static_cast<int>(synopsisLines.size()) > maxSynopsisLines) {
        synopsisLines.resize(static_cast<size_t>(maxSynopsisLines));
        if (!synopsisLines.empty()) {
            synopsisLines.back() = ellipsizeText(synopsisLines.back(), textMaxW, fDetailBodyFont);
        }
    }

    float bodyY = synopsisTop - fmBody.fAscent;
    canvas->save();
    canvas->clipRect(SkRect::MakeLTRB(innerLeft, synopsisTop, innerRight, synopsisMaxBottom), true);
    for (const std::string& line : synopsisLines) {
        sk_sp<SkTextBlob> blob =
            SkTextBlob::MakeFromText(line.c_str(), line.size(), fDetailBodyFont, SkTextEncoding::kUTF8);
        canvas->drawTextBlob(blob, innerLeft, bodyY, fDetailTextPaint);
        bodyY += bodyLineHeight;
    }
    canvas->restore();

    std::string metaLine = formatDetailMetadata(movie);
    metaLine = ellipsizeText(metaLine, textMaxW, fDetailMetaFont);
    sk_sp<SkTextBlob> metaBlob =
        SkTextBlob::MakeFromText(metaLine.c_str(), metaLine.size(), fDetailMetaFont, SkTextEncoding::kUTF8);
    canvas->drawTextBlob(metaBlob, innerLeft, metaBaseline, fDetailTextPaint);
}
