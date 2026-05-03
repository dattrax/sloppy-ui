#include "SkiaRenderer.hpp"
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTextBlob.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "src/gpu/GpuTypesPriv.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
SkSamplingOptions gridSampling() {
    return SkSamplingOptions(SkCubicResampler::Mitchell());
}
}

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
    sk_sp<SkImage> gpu = uploadImageToGpu(raster);
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
            if (!slot.fGridImage && !slot.fBackgroundImage && !slot.fDetailImage) {
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
    if (slot.fState == PosterState::Ready &&
        (slot.fGridImage || slot.fBackgroundImage || slot.fDetailImage || slot.fDetailRaster)) {
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
            slot.fGridImage.reset();
            slot.fBackgroundImage.reset();
            slot.fDetailImage.reset();
            slot.fDetailRaster.reset();
            continue;
        }

        sk_sp<SkImage> raster = SkImages::RasterFromBitmap(*completion.fBitmap);
        if (!raster) {
            slot.fState = PosterState::Failed;
            slot.fGridImage.reset();
            slot.fBackgroundImage.reset();
            slot.fDetailImage.reset();
            slot.fDetailRaster.reset();
            continue;
        }

        slot.fDetailRaster = raster;

        sk_sp<SkImage> fullGpu = uploadImageToGpu(raster);
        if (fullGpu) {
            slot.fDetailImage = fullGpu;
        }

        sk_sp<SkImage> gridRaster = makeScaledImage(raster.get(), fTileTargetWidth, fTileTargetHeight);
        sk_sp<SkImage> gridGpu = uploadImageToGpu(gridRaster ? gridRaster : raster);
        slot.fGridImage = gridGpu ? std::move(gridGpu) : (gridRaster ? std::move(gridRaster) : raster);

        const int halfW = std::max(1, static_cast<int>(std::round(raster->width() * kBackgroundHalfScale)));
        const int halfH = std::max(1, static_cast<int>(std::round(raster->height() * kBackgroundHalfScale)));
        sk_sp<SkImage> bgRaster = makeScaledImage(raster.get(), halfW, halfH);
        sk_sp<SkImage> bgGpu = uploadImageToGpu(bgRaster ? bgRaster : raster);
        slot.fBackgroundImage = bgGpu ? std::move(bgGpu) : (bgRaster ? std::move(bgRaster) : raster);

        if (!slot.fDetailImage) {
            slot.fDetailImage = slot.fGridImage;
        }
        slot.fState = PosterState::Ready;
    }
}

void SkiaRenderer::evictPosterCache(double now, const std::vector<int>&) {
    bool evictedAny = false;
    std::vector<int> resident;
    resident.reserve(fPosterSlots.size());
    for (int i = 0; i < static_cast<int>(fPosterSlots.size()); ++i) {
        PosterSlot& slot = fPosterSlots[i];
        if (slot.fState == PosterState::Ready &&
            (slot.fGridImage || slot.fBackgroundImage || slot.fDetailImage || slot.fDetailRaster)) {
            if (now - slot.fLastUsed > kPosterEvictIdleSeconds) {
                slot.fGridImage.reset();
                slot.fBackgroundImage.reset();
                slot.fDetailImage.reset();
                slot.fDetailRaster.reset();
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
        slot.fGridImage.reset();
        slot.fBackgroundImage.reset();
        slot.fDetailImage.reset();
        slot.fDetailRaster.reset();
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
        if (fBackgroundIndex >= 0 && fBackgroundIndex < static_cast<int>(fMovies.size())) {
            touched.push_back(fBackgroundIndex);
        }
        if (fBackgroundPrevIndex >= 0 && fBackgroundPrevIndex < static_cast<int>(fMovies.size())) {
            touched.push_back(fBackgroundPrevIndex);
        }
    }

    for (int idx : touched) {
        fPosterSlots[idx].fLastUsed = now;
        requestPosterDecode(idx);
    }

    evictPosterCache(now, touched);
}

sk_sp<SkImage> SkiaRenderer::posterForGrid(int movieIndex) const {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fPosterSlots.size())) {
        return fPosterPlaceholder;
    }
    const PosterSlot& slot = fPosterSlots[movieIndex];
    if (slot.fState == PosterState::Ready) {
        if (slot.fGridImage) {
            return slot.fGridImage;
        }
        if (slot.fDetailImage) {
            return slot.fDetailImage;
        }
    }
    return fPosterPlaceholder;
}

sk_sp<SkImage> SkiaRenderer::posterForBackground(int movieIndex) const {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fPosterSlots.size())) {
        return fPosterPlaceholder;
    }
    const PosterSlot& slot = fPosterSlots[movieIndex];
    if (slot.fState == PosterState::Ready) {
        if (slot.fBackgroundImage) {
            return slot.fBackgroundImage;
        }
        if (slot.fDetailImage) {
            return slot.fDetailImage;
        }
    }
    return fPosterPlaceholder;
}

sk_sp<SkImage> SkiaRenderer::posterForDetail(int movieIndex) {
    if (movieIndex < 0 || movieIndex >= static_cast<int>(fPosterSlots.size())) {
        return fPosterPlaceholder;
    }
    PosterSlot& slot = fPosterSlots[movieIndex];
    if (slot.fState != PosterState::Ready) {
        return fPosterPlaceholder;
    }
    if (slot.fDetailImage) {
        return slot.fDetailImage;
    }
    if (slot.fDetailRaster) {
        sk_sp<SkImage> detailGpu = uploadImageToGpu(slot.fDetailRaster);
        if (detailGpu) {
            slot.fDetailImage = detailGpu;
            return slot.fDetailImage;
        }
        return slot.fDetailRaster;
    }
    if (slot.fGridImage) {
        return slot.fGridImage;
    }
    return fPosterPlaceholder;
}

SkISize SkiaRenderer::computeTileTargetSize(int width, int height) {
    const float uiScale = layoutScale(width, height);
    const float pad = kPadding * uiScale;
    const float titleSpace = kTitleSpace * uiScale;
    const float cellW = (static_cast<float>(width) - pad * static_cast<float>(kGridCols + 1)) /
        static_cast<float>(kGridCols);
    const float cellH = (static_cast<float>(height) - pad * static_cast<float>(kGridRows + 1) -
        titleSpace * static_cast<float>(kGridRows)) / static_cast<float>(kGridRows);
    const int targetW = std::max(1, static_cast<int>(std::round(std::max(1.0f, cellW) * kTileOversample)));
    const int targetH = std::max(1, static_cast<int>(std::round(std::max(1.0f, cellH) * kTileOversample)));
    return SkISize::Make(targetW, targetH);
}

void SkiaRenderer::updateTileTargetSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == fLayoutWidth && height == fLayoutHeight && fTileTargetWidth > 0 && fTileTargetHeight > 0) {
        return;
    }
    const SkISize size = computeTileTargetSize(width, height);
    fLayoutWidth = width;
    fLayoutHeight = height;
    fTileTargetWidth = size.width();
    fTileTargetHeight = size.height();
}

sk_sp<SkImage> SkiaRenderer::makeScaledImage(const SkImage* source, int targetWidth, int targetHeight) const {
    if (!source || targetWidth <= 0 || targetHeight <= 0) {
        return nullptr;
    }
    const float srcW = static_cast<float>(source->width());
    const float srcH = static_cast<float>(source->height());
    if (srcW <= 0.0f || srcH <= 0.0f) {
        return nullptr;
    }
    const float scale = std::min(static_cast<float>(targetWidth) / srcW, static_cast<float>(targetHeight) / srcH);
    const int outW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    const int outH = std::max(1, static_cast<int>(std::round(srcH * scale)));
    if (outW == source->width() && outH == source->height()) {
        return sk_ref_sp(source);
    }

    SkBitmap dstBitmap;
    if (!dstBitmap.tryAllocPixels(SkImageInfo::Make(outW, outH, kRGBA_8888_SkColorType, kPremul_SkAlphaType))) {
        return nullptr;
    }
    if (!source->scalePixels(dstBitmap.pixmap(), gridSampling())) {
        return nullptr;
    }
    dstBitmap.setImmutable();
    return SkImages::RasterFromBitmap(dstBitmap);
}

sk_sp<SkImage> SkiaRenderer::uploadImageToGpu(const sk_sp<SkImage>& source) const {
    if (!source || !fContext) {
        return nullptr;
    }
    return SkImages::TextureFromImage(fContext.get(), source.get());
}
