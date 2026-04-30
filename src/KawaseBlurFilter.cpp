#include "KawaseBlurFilter.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
sk_sp<SkImage> makeImageFromShader(GrRecordingContext* context, const sk_sp<SkShader>& shader,
                                   const SkImageInfo& info) {
    if (!context || !shader || info.width() <= 0 || info.height() <= 0) {
        return nullptr;
    }

    sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(context, skgpu::Budgeted::kNo, info);
    if (!surface) {
        return nullptr;
    }

    SkPaint paint;
    paint.setShader(shader);
    paint.setAntiAlias(false);
    surface->getCanvas()->drawPaint(paint);
    return surface->makeImageSnapshot();
}
}  // namespace

KawaseBlurFilter::KawaseBlurFilter() {
    SkString blurString(R"(
        uniform shader child;
        uniform float in_blurOffset;

        half4 main(float2 xy) {
            half4 c = child.eval(xy);
            c += child.eval(xy + float2(+in_blurOffset, +in_blurOffset));
            c += child.eval(xy + float2(+in_blurOffset, -in_blurOffset));
            c += child.eval(xy + float2(-in_blurOffset, -in_blurOffset));
            c += child.eval(xy + float2(-in_blurOffset, +in_blurOffset));
            return half4(c.rgb * 0.2, 1.0);
        }
    )");

    auto [blurEffect, error] = SkRuntimeEffect::MakeForShader(blurString);
    if (!blurEffect) {
        fprintf(stderr, "Runtime shader error: %s\n", error.c_str());
        return;
    }
    fBlurEffect = std::move(blurEffect);
}

sk_sp<SkImage> KawaseBlurFilter::generate(GrRecordingContext* context, uint32_t blurRadius,
                                          const sk_sp<SkImage>& input, const SkRect& blurRect) const {
    if (!context || !input || !fBlurEffect || blurRadius == 0 || blurRect.isEmpty()) {
        return input;
    }

    const float tmpRadius = static_cast<float>(blurRadius) / 2.0f;
    const uint32_t numberOfPasses = std::max(
        1u, std::min(kMaxPasses, static_cast<uint32_t>(std::ceil(tmpRadius))));
    const float radiusByPasses = tmpRadius / static_cast<float>(numberOfPasses);

    const int scaledW = std::max(1, static_cast<int>(std::ceil(blurRect.width() * kInputScale)));
    const int scaledH = std::max(1, static_cast<int>(std::ceil(blurRect.height() * kInputScale)));
    const SkImageInfo scaledInfo = input->imageInfo().makeWH(scaledW, scaledH);

    SkMatrix blurMatrix = SkMatrix::Translate(-blurRect.fLeft, -blurRect.fTop);
    blurMatrix.postScale(kInputScale, kInputScale);

    const SkSamplingOptions linear(SkFilterMode::kLinear, SkMipmapMode::kNone);
    SkRuntimeShaderBuilder blurBuilder(fBlurEffect);
    blurBuilder.child("child") = input->makeShader(linear, blurMatrix);
    blurBuilder.uniform("in_blurOffset") = radiusByPasses * kInputScale;

    sk_sp<SkImage> tmpBlur = makeImageFromShader(context, blurBuilder.makeShader(), scaledInfo);
    if (!tmpBlur) {
        return input;
    }

    for (uint32_t i = 1; i < numberOfPasses; ++i) {
        blurBuilder.child("child") = tmpBlur->makeShader(linear);
        blurBuilder.uniform("in_blurOffset") = static_cast<float>(i) * radiusByPasses * kInputScale;
        sk_sp<SkImage> nextBlur = makeImageFromShader(context, blurBuilder.makeShader(), scaledInfo);
        if (!nextBlur) {
            break;
        }
        tmpBlur = std::move(nextBlur);
    }

    return tmpBlur;
}
