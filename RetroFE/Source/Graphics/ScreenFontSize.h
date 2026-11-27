#pragma once

#include <algorithm>
#include <cmath>

inline float screenFontScale(int width, int height, int layoutWidth, int layoutHeight, int rotation, bool mirror)
{
    if (width <= 0 || height <= 0 || layoutWidth <= 0 || layoutHeight <= 0) {
        return 1.0f;
    }
    if (rotation % 2 != 0) {
        std::swap(width, height);
    }
    float scaleX = static_cast<float>(width) / layoutWidth;
    float scaleY = static_cast<float>(height) / layoutHeight;
    if (mirror) {
        scaleY /= 2.0f;
    }
    return std::max(scaleX, scaleY);
}

inline int screenFontSize(int loadSize, float scale)
{
    if (!(scale > 0.0f) || scale >= 1.0f || loadSize <= 8) {
        return loadSize;
    }
    return std::max(8, static_cast<int>(std::round(loadSize * scale)));
}
