#include "SkiaRenderer.hpp"
#include "PlatformInput.hpp"

#include <algorithm>

void SkiaRenderer::enqueueInputEvent(int key, bool pressed) {
    std::lock_guard<std::mutex> lock(fInputMutex);
    fInputQueue.push(std::make_pair(key, pressed));
}

bool SkiaRenderer::pollInputEvent(std::pair<int, bool>& event) {
    std::lock_guard<std::mutex> lock(fInputMutex);
    if (fInputQueue.empty()) {
        return false;
    }
    event = fInputQueue.front();
    fInputQueue.pop();
    return true;
}

void SkiaRenderer::clearInputQueue() {
    std::lock_guard<std::mutex> lock(fInputMutex);
    while (!fInputQueue.empty()) {
        fInputQueue.pop();
    }
}

void SkiaRenderer::processInputEvent(int key, bool pressed) {
    if (!pressed) return;

    if (fDetailMode) {
        if (key == platform::kKeyEscape) {
            fDetailMode = false;
            fPosterCache = nullptr;
            fPosterCacheWidth = 0;
            fPosterCacheHeight = 0;
            fPosterCacheIndex = -1;
            clearInputQueue();
        }
        return;
    }

    if (fIsScrolling) {
        return;
    }

    const int itemCount = static_cast<int>(fMovies.size());
    if (itemCount <= 0) {
        return;
    }

    const int maxIndex = itemCount - 1;
    const GridScrollLimits limits = computeGridScrollLimits(itemCount);

    if (key == platform::kKeyEnter || key == platform::kKeyKpEnter) {
        if (fFocusIndex >= 0 && fFocusIndex < itemCount) {
            fDetailMode = true;
            fDetailIndex = fFocusIndex;
        }
        return;
    }

    int previousFocus = fFocusIndex;
    int focusRow = fFocusIndex / kGridLayout.cols;
    int focusCol = fFocusIndex % kGridLayout.cols;
    bool changed = false;

    switch (key) {
        case platform::kKeyUp:
            if (focusRow > 0) {
                fFocusIndex -= kGridLayout.cols;
                changed = true;
                focusRow = fFocusIndex / kGridLayout.cols;
                if (focusRow < fScrollOffset && fScrollOffset > 0) {
                    beginVerticalScroll(fScrollOffset - 1, false);
                }
            }
            break;
        case platform::kKeyDown:
            if (focusRow < limits.totalRows - 1) {
                int nextIdx = (focusRow + 1) * kGridLayout.cols + focusCol;
                if (nextIdx > maxIndex) {
                    nextIdx = maxIndex;
                }
                if (nextIdx != fFocusIndex) {
                    fFocusIndex = nextIdx;
                    changed = true;
                }
                focusRow = fFocusIndex / kGridLayout.cols;
                if (focusRow >= fScrollOffset + kGridLayout.rows && fScrollOffset < limits.maxOffset) {
                    beginVerticalScroll(fScrollOffset + 1, true);
                }
            }
            break;
        case platform::kKeyLeft:
            if (focusCol > 0) {
                fFocusIndex--;
                changed = true;
            }
            break;
        case platform::kKeyRight:
            if (focusCol < kGridLayout.cols - 1 && fFocusIndex < maxIndex) {
                fFocusIndex++;
                changed = true;
            }
            break;
    }

    if (changed) {
        fFocusIndex = std::max(0, std::min(fFocusIndex, maxIndex));
        fIsTextScrolling = true;
        float now = static_cast<float>(platform::nowSeconds());
        fScrollingTextStartTime = now;
        if (fFocusIndex != previousFocus) {
            if (fBackgroundIndex < 0 || fBackgroundIndex >= itemCount) {
                fBackgroundIndex = previousFocus;
            }
            fBackgroundPrevIndex = fBackgroundIndex;
            fBackgroundIndex = fFocusIndex;
            fBackgroundFadeStartTime = now;
            fBackgroundFadeProgress = 0.0f;
            fBackgroundFading = (fBackgroundPrevIndex != fBackgroundIndex);
            if (!fBackgroundFading) {
                fBackgroundPrevIndex = -1;
                fBackgroundFadeProgress = 1.0f;
            }
        }
    }

    if (fIsScrolling) {
        clearInputQueue();
    }
}
