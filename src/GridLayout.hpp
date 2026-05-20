#pragma once

#include "ImageFit.hpp"
#include "include/core/SkImage.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <functional>
#include <vector>

struct GridScrollLimits {
    int totalRows = 0;
    int maxOffset = 0;
};

struct GridLayout {
    int cols = 4;
    int rows = 3;
    float paddingDesign = 8.0f;
    float titleSpaceDesign = 32.0f;
    float blurHoleInsetDesign = 1.5f;
    float cornerRadiusDesign = 12.0f;
};

struct GridLayoutMetrics {
    float pad = 0.0f;
    float titleSpace = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float cornerR = 0.0f;
    float holeInset = 0.0f;
};

struct GridCellPlacement {
    int movieIndex = -1;
    SkRect cellRect = SkRect::MakeEmpty();
    SkRect imageRect = SkRect::MakeEmpty();
    SkRect holeRect = SkRect::MakeEmpty();
};

inline GridLayoutMetrics computeGridLayout(const GridLayout& layout,
                                           int width, int height, float uiScale) {
    GridLayoutMetrics metrics;
    metrics.pad = layout.paddingDesign * uiScale;
    metrics.titleSpace = layout.titleSpaceDesign * uiScale;
    metrics.cellH = (static_cast<float>(height) - metrics.pad * static_cast<float>(layout.rows + 1) -
                     metrics.titleSpace * static_cast<float>(layout.rows)) /
        static_cast<float>(layout.rows);
    metrics.cellW = (static_cast<float>(width) - metrics.pad * static_cast<float>(layout.cols + 1)) /
        static_cast<float>(layout.cols);
    metrics.cornerR = layout.cornerRadiusDesign * uiScale;
    metrics.holeInset = layout.blurHoleInsetDesign * uiScale + metrics.cornerR;
    return metrics;
}

inline GridScrollLimits computeGridScrollLimits(const GridLayout& layout, int itemCount) {
    GridScrollLimits limits;
    if (itemCount <= 0) {
        return limits;
    }
    limits.totalRows = itemCount / layout.cols + (itemCount % layout.cols != 0 ? 1 : 0);
    limits.maxOffset = std::max(0, limits.totalRows - layout.rows);
    return limits;
}

inline std::vector<GridCellPlacement> collectVisibleGridPlacements(
    const GridLayout& layout, const GridLayoutMetrics& metrics,
    int scrollOffset, int movieCount, float scrollY,
    const SkRect& screenRect,
    const std::function<sk_sp<SkImage>(int movieIndex)>& posterForIndex) {
    std::vector<GridCellPlacement> placements;
    placements.reserve(static_cast<size_t>(layout.cols * layout.rows));

    for (int row = 0; row < layout.rows; ++row) {
        for (int col = 0; col < layout.cols; ++col) {
            int idx = (scrollOffset + row) * layout.cols + col;
            if (idx >= movieCount) {
                continue;
            }
            sk_sp<SkImage> img = posterForIndex(idx);
            if (!img) {
                continue;
            }

            const float cellX = metrics.pad + static_cast<float>(col) * (metrics.cellW + metrics.pad);
            const float cellY = metrics.pad + static_cast<float>(row) *
                (metrics.cellH + metrics.pad + metrics.titleSpace) - scrollY;
            const float imgW = static_cast<float>(img->width());
            const float imgH = static_cast<float>(img->height());
            if (imgW <= 0.0f || imgH <= 0.0f) {
                continue;
            }

            GridCellPlacement placement;
            placement.movieIndex = idx;
            placement.cellRect = SkRect::MakeXYWH(cellX, cellY, metrics.cellW, metrics.cellH);
            placement.imageRect = fitImageContain(metrics.cellW, metrics.cellH, imgW, imgH, cellX, cellY);
            placement.holeRect = placement.imageRect.makeInset(metrics.holeInset, metrics.holeInset);
            if (placement.holeRect.width() <= 0.0f || placement.holeRect.height() <= 0.0f) {
                continue;
            }
            if (!placement.holeRect.intersect(screenRect)) {
                continue;
            }
            placements.push_back(placement);
        }
    }
    return placements;
}

inline std::vector<int> collectVisibleGridIndices(const GridLayout& layout,
                                                  int scrollOffset, int movieCount) {
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(layout.cols * layout.rows));
    for (int row = 0; row < layout.rows; ++row) {
        for (int col = 0; col < layout.cols; ++col) {
            int idx = (scrollOffset + row) * layout.cols + col;
            if (idx < movieCount) {
                indices.push_back(idx);
            }
        }
    }
    return indices;
}
