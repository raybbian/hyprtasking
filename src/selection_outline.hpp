#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

struct HTSelectionInsets {
    float left, right, top, bottom;
};

struct HTSelectionOutline {
    float x, y, width, height;
    int border, radius;
    bool inWallpaperGap;
};

// The border pass draws outside its box; these coordinates are in tile space.
inline std::optional<HTSelectionOutline> ht_selection_outline(
    float x,
    float y,
    float width,
    float height,
    float monitorWidth,
    float monitorScale,
    HTSelectionInsets reserved,
    HTSelectionInsets gaps,
    float windowRadius,
    bool fullscreen = false
) {
    if (width <= 0 || height <= 0 || monitorWidth <= 0 || monitorScale <= 0)
        return std::nullopt;

    const float previewScale = width / monitorWidth;
    const float factor = monitorScale * previewScale;
    const float gap = std::min({gaps.left, gaps.right, gaps.top, gaps.bottom}) * factor;

    const auto tileOutline = [&]() -> std::optional<HTSelectionOutline> {
        const int border = 1;
        const float inset = border * monitorScale + 1.f;
        const float w = width - 2.f * inset;
        const float h = height - 2.f * inset;
        if (w <= border * monitorScale * 2 || h <= border * monitorScale * 2)
            return std::nullopt;
        const int radius = std::clamp(
            static_cast<int>(std::lround(std::max(0.f, windowRadius) * previewScale)),
            0,
            static_cast<int>(std::floor(std::min(w, h) * .5f / monitorScale))
        );
        return HTSelectionOutline {x + inset, y + inset, w, h, border, radius, false};
    };

    if (fullscreen || gap < 2.f)
        return tileOutline();

    const int border = std::max(1, static_cast<int>(std::lround((gap - .75f) / monitorScale)));
    if (border * monitorScale > gap - .25f)
        return tileOutline();

    const float left = (reserved.left + gaps.left) * factor;
    const float right = (reserved.right + gaps.right) * factor;
    const float top = (reserved.top + gaps.top) * factor;
    const float bottom = (reserved.bottom + gaps.bottom) * factor;
    const float w = width - left - right;
    const float h = height - top - bottom;
    if (w <= border * monitorScale * 2 || h <= border * monitorScale * 2)
        return tileOutline();

    const int radius = std::clamp(
        static_cast<int>(std::lround(std::max(0.f, windowRadius) * previewScale + border)),
        0,
        static_cast<int>(std::floor(std::min(w, h) * .5f / monitorScale))
    );
    return HTSelectionOutline {x + left, y + top, w, h, border, radius, true};
}
