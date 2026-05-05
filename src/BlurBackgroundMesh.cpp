#include "BlurBackgroundMesh.hpp"

#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

void subtractXIntervals(std::vector<std::pair<float, float>>& intervals, float lo, float hi) {
    if (hi <= lo) {
        return;
    }
    std::vector<std::pair<float, float>> next;
    for (const auto& p : intervals) {
        float L = p.first;
        float R = p.second;
        if (R <= lo || L >= hi) {
            next.push_back(p);
            continue;
        }
        if (L < lo) {
            next.emplace_back(L, std::min(R, lo));
        }
        if (R > hi) {
            next.emplace_back(std::max(L, hi), R);
        }
    }
    intervals.swap(next);
}

void appendRectTriangles(std::vector<SkPoint>& positions, std::vector<SkPoint>& texs,
                         std::vector<uint16_t>& indices, float x0, float y0, float x1, float y1,
                         float texScaleX, float texScaleY) {
    uint16_t base = static_cast<uint16_t>(positions.size());
    positions.push_back(SkPoint::Make(x0, y0));
    positions.push_back(SkPoint::Make(x1, y0));
    positions.push_back(SkPoint::Make(x1, y1));
    positions.push_back(SkPoint::Make(x0, y1));
    texs.push_back(SkPoint::Make(x0 * texScaleX, y0 * texScaleY));
    texs.push_back(SkPoint::Make(x1 * texScaleX, y0 * texScaleY));
    texs.push_back(SkPoint::Make(x1 * texScaleX, y1 * texScaleY));
    texs.push_back(SkPoint::Make(x0 * texScaleX, y1 * texScaleY));
    indices.push_back(static_cast<uint16_t>(base + 0));
    indices.push_back(static_cast<uint16_t>(base + 1));
    indices.push_back(static_cast<uint16_t>(base + 2));
    indices.push_back(static_cast<uint16_t>(base + 0));
    indices.push_back(static_cast<uint16_t>(base + 2));
    indices.push_back(static_cast<uint16_t>(base + 3));
}

}  // namespace

sk_sp<SkVertices> makeBlurredBackgroundMesh(
    const BlurBackgroundMeshParams& p,
    const std::function<sk_sp<SkImage>(int movieIndex)>& posterForGrid) {

    const float wf = static_cast<float>(p.width);
    const float hf = static_cast<float>(p.height);
    const float pad = p.paddingDesign * p.uiScale;
    const float titleSpace = p.titleSpaceDesign * p.uiScale;
    const float cellH = (static_cast<float>(p.height) - pad * static_cast<float>(p.gridRows + 1) -
                            titleSpace * static_cast<float>(p.gridRows)) /
                        static_cast<float>(p.gridRows);
    const float cellW = (static_cast<float>(p.width) - pad * static_cast<float>(p.gridCols + 1)) /
                        static_cast<float>(p.gridCols);
    const float cornerR = p.cornerRadiusDesign * p.uiScale;
    const float holeInset = p.blurHoleInsetDesign * p.uiScale + cornerR;
    const SkRect screenRect = SkRect::MakeWH(wf, hf);

    std::vector<SkRect> holes;
    holes.reserve(static_cast<size_t>(p.gridCols * p.gridRows));

    for (int row = 0; row < p.gridRows; ++row) {
        for (int col = 0; col < p.gridCols; ++col) {
            int idx = (p.scrollOffset + row) * p.gridCols + col;
            if (idx >= p.movieCount) {
                continue;
            }
            sk_sp<SkImage> img = posterForGrid(idx);
            if (!img) {
                continue;
            }

            float cellX = pad + static_cast<float>(col) * (cellW + pad);
            float cellY = pad + static_cast<float>(row) * (cellH + pad + titleSpace) - p.scrollY;
            float imgW = static_cast<float>(img->width());
            float imgH = static_cast<float>(img->height());
            float scale = std::min(cellW / imgW, cellH / imgH);
            float dstW = imgW * scale;
            float dstH = imgH * scale;
            float dstX = cellX + (cellW - dstW) * 0.5f;
            float dstY = cellY + (cellH - dstH) * 0.5f;
            SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
            SkRect holeRect = dstRect.makeInset(holeInset, holeInset);
            if (holeRect.width() <= 0.0f || holeRect.height() <= 0.0f) {
                continue;
            }
            if (!holeRect.intersect(screenRect)) {
                continue;
            }
            holes.push_back(holeRect);
        }
    }

    std::vector<float> ys;
    ys.reserve(2 + holes.size() * 2);
    ys.push_back(0.0f);
    ys.push_back(hf);
    for (const SkRect& h : holes) {
        ys.push_back(std::max(0.0f, h.top()));
        ys.push_back(std::min(hf, h.bottom()));
    }
    std::sort(ys.begin(), ys.end());
    std::vector<float> ysCollapsed;
    ysCollapsed.reserve(ys.size());
    for (float y : ys) {
        if (ysCollapsed.empty() || std::abs(y - ysCollapsed.back()) >= 1e-4f) {
            ysCollapsed.push_back(y);
        }
    }

    std::vector<SkPoint> positions;
    std::vector<SkPoint> texs;
    std::vector<uint16_t> indices;
    positions.reserve(256);
    texs.reserve(256);
    indices.reserve(384);

    for (size_t i = 0; i + 1 < ysCollapsed.size(); ++i) {
        float y0 = ysCollapsed[i];
        float y1 = ysCollapsed[i + 1];
        if (y1 - y0 <= 1e-4f) {
            continue;
        }
        std::vector<std::pair<float, float>> intervals;
        intervals.emplace_back(0.0f, wf);

        for (const SkRect& h : holes) {
            if (h.bottom() <= y0 || h.top() >= y1) {
                continue;
            }
            float lo = std::max(0.0f, h.left());
            float hi = std::min(wf, h.right());
            subtractXIntervals(intervals, lo, hi);
        }

        for (const auto& iv : intervals) {
            appendRectTriangles(positions, texs, indices, iv.first, y0, iv.second, y1, p.texScaleX,
                                p.texScaleY);
        }
    }

    if (positions.empty()) {
        return nullptr;
    }

    return SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(positions.size()),
        positions.data(), texs.data(), nullptr, static_cast<int>(indices.size()), indices.data());
}
