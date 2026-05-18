#include "BlurBackgroundCache.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/GrTypes.h"

namespace {

SkSamplingOptions backgroundSampling() {
    return SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone);
}

}  // namespace

BlurBackgroundCache::BlurBackgroundCache(const BlurBackgroundCacheConfig& config)
    : fBlurRadius(config.blurRadius) {}

BlurBackgroundCache::Slot& BlurBackgroundCache::slot(bool previousSlot) {
    return previousSlot ? fPrevious : fCurrent;
}

const BlurBackgroundCache::Slot& BlurBackgroundCache::slot(bool previousSlot) const {
    return previousSlot ? fPrevious : fCurrent;
}

void BlurBackgroundCache::clearSlot(Slot& slotState) {
    slotState.image.reset();
    slotState.index = -1;
    slotState.generation = 0;
    slotState.sourceImageId = 0;
}

void BlurBackgroundCache::invalidate() {
    clearSlot(fCurrent);
    clearSlot(fPrevious);
    fCacheWidth = 0;
    fCacheHeight = 0;
}

void BlurBackgroundCache::invalidateForIndex(int movieIndex) {
    if (fCurrent.index == movieIndex) {
        clearSlot(fCurrent);
    }
    if (fPrevious.index == movieIndex) {
        clearSlot(fPrevious);
    }
}

sk_sp<SkImage> BlurBackgroundCache::buildBlurred(GrDirectContext* context,
                                                 const sk_sp<SkImage>& source,
                                                 int width, int height) const {
    if (!context || !source || width <= 0 || height <= 0) {
        return nullptr;
    }

    const float imgW = static_cast<float>(source->width());
    const float imgH = static_cast<float>(source->height());
    if (imgW <= 0.0f || imgH <= 0.0f) {
        return nullptr;
    }

    const SkImageInfo bgInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> backgroundSurface = SkSurfaces::RenderTarget(context, skgpu::Budgeted::kNo, bgInfo);
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

    return fBlurFilter.generate(context, fBlurRadius, composed, backgroundRect);
}

sk_sp<SkImage> BlurBackgroundCache::ensure(bool previousSlot, GrDirectContext* context,
                                           int movieIndex, int width, int height,
                                           const sk_sp<SkImage>& source, uint32_t generation) {
    Slot& cached = slot(previousSlot);

    if (movieIndex < 0) {
        clearSlot(cached);
        return nullptr;
    }

    if (width != fCacheWidth || height != fCacheHeight) {
        invalidate();
        fCacheWidth = width;
        fCacheHeight = height;
    }

    const uint32_t sourceImageId = source ? source->uniqueID() : 0;
    if (cached.image && cached.index == movieIndex && cached.generation == generation &&
        cached.sourceImageId == sourceImageId) {
        return cached.image;
    }

    cached.image = buildBlurred(context, source, width, height);
    cached.index = movieIndex;
    cached.generation = generation;
    cached.sourceImageId = sourceImageId;
    return cached.image;
}
