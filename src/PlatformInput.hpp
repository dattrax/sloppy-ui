#pragma once

#include <chrono>

namespace platform {

inline double nowSeconds() {
    static const auto kStart = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = now - kStart;
    return elapsed.count();
}

static constexpr int kKeyEscape = 256;
static constexpr int kKeyEnter = 257;
static constexpr int kKeyKpEnter = 335;
static constexpr int kKeyRight = 262;
static constexpr int kKeyLeft = 263;
static constexpr int kKeyDown = 264;
static constexpr int kKeyUp = 265;

}  // namespace platform
