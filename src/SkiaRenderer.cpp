#include "SkiaRenderer.hpp"
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/utils/SkShadowUtils.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstdint>
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

    fRedPaint.setAntiAlias(true);
    fBluePaint.setAntiAlias(true);

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
            sk_sp<SkImage> img = SkImages::RasterFromBitmap(bitmap);
            if (!img) continue;
            fPosterImages.push_back(std::move(img));
        }
    }

    return true;
}

void SkiaRenderer::destroy() {
    fPosterImages.clear();
    fContext.reset();
}

 void SkiaRenderer::draw(SkCanvas* canvas, int width, int height, float time) {
    (void)time;
    (void)width;
    (void)height;

    if (fIsScrolling) {
        float scrollTime = time - fScrollStartTime;
        if (scrollTime >= kScrollDuration) {
            fScrollProgress = 1.0f;
            fScrollOffset = fTargetOffset;
            fIsScrolling = false;
            // Clamp selection to visible range after scroll completes
            int visibleRows = kGridRows;
            int totalRows = static_cast<int>(fPosterImages.size() / kGridCols + (fPosterImages.size() % kGridCols != 0));
            int maxOffset = totalRows - visibleRows;
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

    const float padding = 8.f;
    const float cornerRadius = 12.f;
    const float shadowBlur = 8.f;
    const float selectionOffset = 4.f;
    const float selectionStrokeWidth = 3.f;

    const float cellW = (width - padding * (kGridCols + 1)) / kGridCols;
    const float cellH = (height - padding * (kGridRows + 1)) / kGridRows;

    SkPaint selectionPaint;
    selectionPaint.setStyle(SkPaint::kStroke_Style);
    selectionPaint.setColor(SK_ColorWHITE);
    selectionPaint.setStrokeWidth(selectionStrokeWidth);
    selectionPaint.setAntiAlias(true);

    for (int row = 0; row < kGridRows; ++row) {
        for (int col = 0; col < kGridCols; ++col) {
            int idx = (fScrollOffset + row) * kGridCols + col;
            if (idx >= static_cast<int>(fPosterImages.size())) {
                continue;
            }
            const sk_sp<SkImage>& img = fPosterImages[idx];
            if (!img) continue;

            float cellX = padding + col * (cellW + padding);
            float cellY = padding + row * (cellH + padding) - scrollY;

            float imgW = static_cast<float>(img->width());
            float imgH = static_cast<float>(img->height());
            float scale = std::min(cellW / imgW, cellH / imgH);
            float dstW = imgW * scale;
            float dstH = imgH * scale;
            float dstX = cellX + (cellW - dstW) * 0.5f;
            float dstY = cellY + (cellH - dstH) * 0.5f;

            SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);
            SkRRect rrect = SkRRect::MakeRectXY(dstRect, cornerRadius, cornerRadius);

            SkPath path = SkPath::RRect(rrect);

            SkPoint3 zPlaneParams = SkPoint3::Make(0, 0, 0);
            SkPoint3 lightPos = SkPoint3::Make(width * 0.25f, -height * 0.5f, 400);
            SkScalar lightRadius = shadowBlur * 4;
            SkColor ambientColor = SkColorSetA(SK_ColorBLACK, 0x30);
            SkColor spotColor = SkColorSetA(SK_ColorBLACK, 0x60);
            SkShadowUtils::DrawShadow(canvas, path, zPlaneParams, lightPos, lightRadius,
                                      ambientColor, spotColor);

            canvas->save();
            canvas->clipRRect(rrect, true);
            SkRect src = SkRect::MakeIWH(img->width(), img->height());
            canvas->drawImageRect(img, src, dstRect, SkSamplingOptions(), nullptr,
                                 SkCanvas::kFast_SrcRectConstraint);

            canvas->restore();

            if (idx == fSelectedRow * kGridCols + fSelectedCol) {
                SkRect selRect = dstRect.makeOutset(selectionOffset, selectionOffset);
                SkRRect selRRect = SkRRect::MakeRectXY(selRect, cornerRadius + selectionOffset,
                                                      cornerRadius + selectionOffset);
                canvas->drawRRect(selRRect, selectionPaint);
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
