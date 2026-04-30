#pragma once

#include "include/core/SkImage.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/gpu/ganesh/GrRecordingContext.h"
#include <cstdint>

class KawaseBlurFilter {
public:
    KawaseBlurFilter();

    sk_sp<SkImage> generate(GrRecordingContext* context, uint32_t blurRadius,
                            const sk_sp<SkImage>& input, const SkRect& blurRect) const;

private:
    static constexpr uint32_t kMaxPasses = 4;
    static constexpr float kInputScale = 0.25f;

    sk_sp<SkRuntimeEffect> fBlurEffect;
};
