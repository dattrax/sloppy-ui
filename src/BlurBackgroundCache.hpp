/*
 * Caches blurred full-frame background images for current/previous cross-fade slots.
 */

#pragma once

#include "KawaseBlurFilter.hpp"
#include "include/core/SkImage.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include <cstdint>

struct BlurBackgroundCacheConfig {
    uint32_t blurRadius = 16;
    float dimRgbFactor = 1.0f;
};

class BlurBackgroundCache {
public:
    explicit BlurBackgroundCache(const BlurBackgroundCacheConfig& config = {});

    void invalidate();
    void invalidateForIndex(int movieIndex);

    sk_sp<SkImage> ensure(bool previousSlot, GrDirectContext* context,
                          int movieIndex, int width, int height,
                          const sk_sp<SkImage>& source, uint32_t generation);

private:
    struct Slot {
        sk_sp<SkImage> image;
        int index = -1;
        uint32_t generation = 0;
        uint32_t sourceImageId = 0;
    };

    Slot& slot(bool previousSlot);
    const Slot& slot(bool previousSlot) const;
    void clearSlot(Slot& slot);

    sk_sp<SkImage> buildBlurred(GrDirectContext* context,
                                const sk_sp<SkImage>& source,
                                int width, int height) const;

    uint32_t fBlurRadius;
    float fDimRgbFactor;
    KawaseBlurFilter fBlurFilter;
    Slot fCurrent;
    Slot fPrevious;
    int fCacheWidth = 0;
    int fCacheHeight = 0;
};
