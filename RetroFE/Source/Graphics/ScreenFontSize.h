#pragma once

#include <algorithm>
#include <cmath>

inline int screenFontSize(int loadSize, float scale)
{
    if (!(scale > 0.0f) || scale >= 1.0f || loadSize <= 8) {
        return loadSize;
    }
    return std::max(8, static_cast<int>(std::round(loadSize * scale)));
}
