#pragma once

#include "include/core/SkRect.h"

#include <algorithm>

inline SkRect fitImageCover(float viewW, float viewH, float imgW, float imgH) {
    const float scale = std::max(viewW / imgW, viewH / imgH);
    const float dstW = imgW * scale;
    const float dstH = imgH * scale;
    const float dstX = (viewW - dstW) * 0.5f;
    const float dstY = (viewH - dstH) * 0.5f;
    return SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
}

inline SkRect fitImageContain(float boxW, float boxH, float imgW, float imgH, float boxX, float boxY) {
    const float scale = std::min(boxW / imgW, boxH / imgH);
    const float dstW = imgW * scale;
    const float dstH = imgH * scale;
    const float dstX = boxX + (boxW - dstW) * 0.5f;
    const float dstY = boxY + (boxH - dstH) * 0.5f;
    return SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
}
