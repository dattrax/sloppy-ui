/*
 * Builds a triangle mesh for drawing a full-frame blurred background with
 * rectangular holes where opaque grid posters sit (avoids wasted sampling).
 */

#pragma once

#include "include/core/SkImage.h"
#include "include/core/SkVertices.h"

#include <functional>

class BlurBackgroundMeshBuilder {
public:
    struct Layout {
        int gridCols = 4;
        int gridRows = 3;
        float paddingDesign = 8.0f;
        float titleSpaceDesign = 32.0f;
        float blurHoleInsetDesign = 1.5f;
        float cornerRadiusDesign = 12.0f;
    };

    explicit BlurBackgroundMeshBuilder(Layout layout);

    struct FrameParams {
        int width = 0;
        int height = 0;
        float uiScale = 1.0f;
        float scrollY = 0.0f;
        float texScaleX = 1.0f;
        float texScaleY = 1.0f;
        int scrollOffset = 0;
        int movieCount = 0;
    };

    sk_sp<SkVertices> build(
        const FrameParams& frame,
        const std::function<sk_sp<SkImage>(int movieIndex)>& posterForGrid) const;

private:
    Layout fLayout;
};
