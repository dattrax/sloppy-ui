#include "SkiaRenderer.hpp"
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_empty.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/utils/SkShadowUtils.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <GLFW/glfw3.h>

bool SkiaRenderer::create(const CreateInfo& info) {
    info.backendContext->fMemoryAllocator =
        skgpu::VulkanMemoryAllocators::Make(*info.backendContext, skgpu::ThreadSafe::kNo);

    fContext = GrDirectContexts::MakeVulkan(*info.backendContext);
    if (!fContext) {
        fprintf(stderr, "Failed to create GrDirectContext (Vulkan).\n");
        destroy();
        return false;
    }

    info.extensions->init(
        info.backendContext->fGetProc,
        info.instance,
        info.physicalDevice,
        info.instanceExtensionCount,
        info.instanceExtensionNames,
        info.deviceExtensionCount,
        info.deviceExtensionNames);

    if (!fMovies.loadFromFile("movies.json")) {
        fprintf(stderr, "Warning: could not load movies.json (run from build dir or set cwd)\n");
    } else {
        fPosterImages.reserve(fMovies.size());
        for (size_t i = 0; i < fMovies.size(); ++i) {
            std::string path = "assets/" + fMovies[i].filename;
            sk_sp<SkData> data = SkData::MakeFromFileName(path.c_str());
            if (!data) continue;
            std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(data);
            if (!codec) continue;
            SkBitmap bitmap;
            if (!bitmap.tryAllocPixels(codec->getInfo())) continue;
            if (codec->getPixels(bitmap.info(), bitmap.getPixels(), bitmap.rowBytes()) != SkCodec::kSuccess) continue;
            bitmap.setImmutable();
            sk_sp<SkImage> raster = SkImages::RasterFromBitmap(bitmap);
            if (!raster) continue;
            sk_sp<SkImage> gpu = SkImages::TextureFromImage(fContext.get(), raster.get());
            fPosterImages.push_back(gpu ? std::move(gpu) : std::move(raster));
        }
    }

    sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
    fTypeface = mgr->makeFromFile("assets/Roboto-Regular.ttf");
    if (!fTypeface) {
        fprintf(stderr, "Warning: could not load any font for text rendering\n");
    }

    fTitleFont = SkFont(fTypeface, kTitleFontSize);

    fTitlePaint.setAntiAlias(true);
    fTitlePaint.setColor(SK_ColorWHITE);

    fSelectionPaint.setStyle(SkPaint::kStroke_Style);
    fSelectionPaint.setColor(SK_ColorWHITE);
    fSelectionPaint.setStrokeWidth(3.0f);
    fSelectionPaint.setAntiAlias(true);

    fMatrix.setScale(0.8f, 0.8f, 0.8f, 1.0f);
    fDimPaint.setColorFilter(SkColorFilters::Matrix(fMatrix));

    return true;
}

void SkiaRenderer::destroy() {
    fTitleCache.clear();
    fPosterImages.clear();
    fContext.reset();
}

void SkiaRenderer::draw(SkCanvas* canvas, int width, int height, float time) {
    if (fIsScrolling) {
        float scrollTime = time - fScrollStartTime;
        if (scrollTime >= kScrollDuration) {
            fScrollProgress = 1.0f;
            fScrollOffset = fTargetOffset;
            fIsScrolling = false;
            int visibleRows = kGridRows;
            if (fSelectedRow < fScrollOffset) {
                fSelectedRow = fScrollOffset;
            } else if (fSelectedRow >= fScrollOffset + visibleRows) {
                fSelectedRow = fScrollOffset + visibleRows - 1;
            }
        } else {
            fScrollProgress = scrollTime / kScrollDuration;
        }
    }

    float scrollY = 0.0f;
    if (fIsScrolling) {
        float t = easeInOut(fScrollProgress);
        scrollY = t * (height / kGridRows) * (fScrollingDown ? 1.0f : -1.0f);
    }

    canvas->clear(SK_ColorGRAY);

    const float cellW = (width - kPadding * (kGridCols + 1)) / kGridCols;
    const float cellH = (height - kPadding * (kGridRows + 1) - kTitleSpace * kGridRows) / kGridRows;

    if (cellW != fCachedCellW) {
        rebuildTitleCache(cellW);
    }

    const SkPoint3 zPlaneParams = SkPoint3::Make(0, 0, 0);
    const SkPoint3 lightPos = SkPoint3::Make(width * 0.25f, -height * 0.5f, 400);
    const SkScalar lightRadius = 32.0f;
    const SkColor ambientColor = SkColorSetA(SK_ColorBLACK, 0x30);
    const SkColor spotColor = SkColorSetA(SK_ColorBLACK, 0x60);
    const int selectedIdx = fSelectedRow * kGridCols + fSelectedCol;
    const int selectedRowGlobal = fScrollOffset + fSelectedRow;

    for (int row = 0; row < kGridRows; ++row) {
        bool rowHasHighlight = (fScrollOffset + row) == selectedRowGlobal;

        for (int col = 0; col < kGridCols; ++col) {
            int idx = (fScrollOffset + row) * kGridCols + col;
            if (idx >= static_cast<int>(fPosterImages.size())) {
                continue;
            }
            const sk_sp<SkImage>& img = fPosterImages[idx];
            if (!img) continue;

            float cellX = kPadding + col * (cellW + kPadding);
            float cellY = kPadding + row * (cellH + kPadding + kTitleSpace) - scrollY;

            float imgW = static_cast<float>(img->width());
            float imgH = static_cast<float>(img->height());
            float scale = std::min(cellW / imgW, cellH / imgH);
            float dstW = imgW * scale;
            float dstH = imgH * scale;
            float dstX = cellX + (cellW - dstW) * 0.5f;
            float dstY = cellY + (cellH - dstH) * 0.5f;

            SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
            SkRRect rrect = SkRRect::MakeRectXY(dstRect, kCornerRadius, kCornerRadius);

            SkShadowUtils::DrawShadow(canvas, SkPath::RRect(rrect),
                                      zPlaneParams, lightPos, lightRadius,
                                      ambientColor, spotColor);

            canvas->save();
            canvas->clipRRect(rrect, true);
            canvas->drawImageRect(img, dstRect, SkSamplingOptions(), rowHasHighlight ? nullptr : &fDimPaint);
            canvas->restore();

            if (idx == selectedIdx) {
                SkRect selRect = dstRect.makeOutset(kSelectionOffset, kSelectionOffset);
                SkRRect selRRect = SkRRect::MakeRectXY(selRect,
                    kCornerRadius + kSelectionOffset, kCornerRadius + kSelectionOffset);
                canvas->drawRRect(selRRect, fSelectionPaint);
            }

            if (idx < static_cast<int>(fTitleCache.size()) && fTitleCache[idx].blob) {
                float titleX = cellX + (cellW - fTitleCache[idx].width) * 0.5f;
                float titleY = cellY + cellH + kTitleSpace * 0.75f;
                canvas->drawTextBlob(fTitleCache[idx].blob, titleX, titleY, fTitlePaint);
            }
        }
    }
}

bool SkiaRenderer::flushAndSubmit(SkSurface* surface, VkSemaphore signalSemaphore, uint32_t presentQueueIndex) {
    GrBackendSemaphore beSignal = GrBackendSemaphores::MakeVk(signalSemaphore);
    GrFlushInfo flushInfo;
    flushInfo.fNumSemaphores = 1;
    flushInfo.fSignalSemaphores = &beSignal;
    skgpu::MutableTextureState presentState = skgpu::MutableTextureStates::MakeVulkan(
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, presentQueueIndex);
    fContext->flush(surface, flushInfo, &presentState);
    fContext->submit();
    return true;
}

float SkiaRenderer::easeInOut(float t) const {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(2.0f * (1.0f - t), 3.0f) / 2.0f;
}

void SkiaRenderer::rebuildTitleCache(float cellW) {
    fCachedCellW = cellW;
    fTitleCache.resize(fMovies.size());
    float maxWidth = cellW * 0.9f;

    for (size_t i = 0; i < fMovies.size(); ++i) {
        std::string display = ellipsizeText(fMovies[i].title, maxWidth, fTitleFont);
        float w = fTitleFont.measureText(display.c_str(), display.size(), SkTextEncoding::kUTF8);
        fTitleCache[i].text = std::move(display);
        fTitleCache[i].width = w;
        fTitleCache[i].blob = SkTextBlob::MakeFromText(
            fTitleCache[i].text.c_str(), fTitleCache[i].text.size(),
            fTitleFont, SkTextEncoding::kUTF8);
    }
}

std::string SkiaRenderer::ellipsizeText(const std::string& text, float maxWidth, SkFont& font) {
    if (text.empty()) return text;

    float textWidth = font.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);
    if (textWidth <= maxWidth) {
        return text;
    }

    static const std::string ellipsis = "...";
    float ellipsisWidth = font.measureText(ellipsis.c_str(), ellipsis.size(), SkTextEncoding::kUTF8);
    if (ellipsisWidth >= maxWidth) {
        return "";
    }

    float availableWidth = maxWidth - ellipsisWidth;

    size_t lo = 0, hi = text.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        float w = font.measureText(text.c_str(), mid, SkTextEncoding::kUTF8);
        if (w <= availableWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return text.substr(0, lo) + ellipsis;
}

void SkiaRenderer::handleKey(int key, bool pressed) {
    if (!pressed) return;

    int visibleRows = kGridRows;
    int totalRows = static_cast<int>(fPosterImages.size() / kGridCols + (fPosterImages.size() % kGridCols != 0));
    int maxOffset = totalRows - visibleRows;

   switch (key) {
   case GLFW_KEY_UP:
            if (fSelectedRow > 0) {
                fSelectedRow--;
                if (fSelectedRow < fScrollOffset && fScrollOffset > 0) {
                    fTargetOffset = fScrollOffset - 1;
                    fIsScrolling = true;
                    fScrollProgress = 0.0f;
                    fScrollStartTime = glfwGetTime();
                    fScrollingDown = false;
                }
            }
            break;
    case GLFW_KEY_DOWN:
            if (fSelectedRow < totalRows - 1) {
                fSelectedRow++;
                if (fSelectedRow >= fScrollOffset + visibleRows && fScrollOffset < maxOffset) {
                    fTargetOffset = fScrollOffset + 1;
                    fIsScrolling = true;
                    fScrollProgress = 0.0f;
                    fScrollStartTime = glfwGetTime();
                    fScrollingDown = true;
                }
            }
            break;
        case GLFW_KEY_LEFT:
            if (fSelectedCol > 0) fSelectedCol--;
            break;
        case GLFW_KEY_RIGHT:
            if (fSelectedCol < kGridCols - 1) fSelectedCol++;
            break;
    }
}
