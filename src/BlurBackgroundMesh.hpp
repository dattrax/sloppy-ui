/*
 * Builds a triangle mesh for drawing a full-frame blurred background with
 * rectangular holes where opaque grid posters sit (avoids wasted sampling).
 */

#pragma once

#include "include/core/SkImage.h"
#include "include/core/SkVertices.h"

#include <functional>

struct BlurBackgroundMeshParams {
    int width = 0;
    int height = 0;
    float uiScale = 1.0f;
    float scrollY = 0.0f;
    float texScaleX = 1.0f;
    float texScaleY = 1.0f;
    int scrollOffset = 0;
    int movieCount = 0;
    int gridCols = 4;
    int gridRows = 3;
    float paddingDesign = 8.0f;
    float titleSpaceDesign = 32.0f;
    float blurHoleInsetDesign = 1.5f;
    float cornerRadiusDesign = 12.0f;
};

sk_sp<SkVertices> makeBlurredBackgroundMesh(
    const BlurBackgroundMeshParams& params,
    const std::function<sk_sp<SkImage>(int movieIndex)>& posterForGrid);
