/*
 * Builds a triangle mesh for drawing a full-frame blurred background with
 * rectangular holes where opaque grid posters sit (avoids wasted sampling).
 */

#pragma once

#include "GridLayout.hpp"
#include "include/core/SkImage.h"
#include "include/core/SkVertices.h"

#include <functional>

class BlurBackgroundMeshBuilder {
public:
    explicit BlurBackgroundMeshBuilder(GridLayout layout);

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
    GridLayout fLayout;
};
