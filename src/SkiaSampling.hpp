#pragma once

#include "include/core/SkSamplingOptions.h"

inline SkSamplingOptions skLinearSampling() {
    return SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone);
}
