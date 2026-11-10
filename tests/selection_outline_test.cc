#include "../src/selection_outline.hpp"

#include <cassert>
#include <cmath>

int main() {
    constexpr HTSelectionInsets reserved {60, 10, 10, 10};
    constexpr HTSelectionInsets gaps {10, 10, 10, 10};
    const auto selected = ht_selection_outline(960, 0, 960, 600, 2880, 1.5f, reserved, gaps, 15);
    assert(selected && selected->inWallpaperGap);
    assert(selected->border == 3);
    assert(selected->radius == 8);
    assert(std::abs(selected->x - 995.f) < 0.01f);
    assert(std::abs(selected->x + selected->width - 1910.f) < 0.01f);
    assert(std::abs(selected->y - 10.f) < 0.01f);
    assert(std::abs(selected->y + selected->height - 590.f) < 0.01f);

    const auto neighbor = ht_selection_outline(0, 600, 960, 600, 2880, 1.5f, reserved, gaps, 15);
    assert(
        neighbor && std::abs(neighbor->x - 35.f) < 0.01f && std::abs(neighbor->y - 610.f) < 0.01f
    );

    const auto zeroGaps = ht_selection_outline(0, 0, 960, 600, 2880, 1.5f, reserved, {}, 15);
    assert(zeroGaps && !zeroGaps->inWallpaperGap && zeroGaps->border == 1);
    assert(zeroGaps->x > 0 && zeroGaps->y > 0);
    assert(zeroGaps->x + zeroGaps->width < 960);
    assert(zeroGaps->y + zeroGaps->height < 600);
    const auto asymmetricZero =
        ht_selection_outline(0, 0, 960, 600, 2880, 1.5f, reserved, {10, 10, 0, 10}, 15);
    assert(asymmetricZero && !asymmetricZero->inWallpaperGap);

    const auto fullscreen =
        ht_selection_outline(960, 0, 960, 600, 2880, 1.5f, reserved, gaps, 15, true);
    assert(fullscreen && !fullscreen->inWallpaperGap);
    assert(fullscreen->border == 1 && fullscreen->radius > 0);
    assert(fullscreen->x > 960 && fullscreen->x + fullscreen->width < 1920);
    assert(!ht_selection_outline(0, 0, 0, 600, 2880, 1.5f, reserved, gaps, 15));
    assert(!ht_selection_outline(0, 0, 960, 600, 0, 1.5f, reserved, gaps, 15));
    assert(!ht_selection_outline(0, 0, 960, 600, 2880, 0, reserved, gaps, 15));

    const auto asymmetric =
        ht_selection_outline(0, 0, 800, 450, 2400, 1.25f, {0, 50, 25, 0}, {5, 15, 8, 10}, 18);
    assert(asymmetric && asymmetric->inWallpaperGap && asymmetric->x > 2.f && asymmetric->x < 2.2f);
    assert(asymmetric->width > 760 && asymmetric->height < 450);
    assert(asymmetric->radius > 0);

    const auto hidpi = ht_selection_outline(0, 0, 640, 360, 1920, 2.f, reserved, gaps, 15);
    assert(hidpi && hidpi->border == 3 && hidpi->radius > 0);
    const auto thin = ht_selection_outline(0, 0, 960, 600, 2880, 1.5f, reserved, {4, 4, 4, 4}, 15);
    assert(thin && thin->border == 1);

    const auto dpiFallback =
        ht_selection_outline(0, 0, 600, 400, 1800, 3.f, reserved, {2, 2, 2, 2}, 15);
    assert(dpiFallback && !dpiFallback->inWallpaperGap && dpiFallback->border == 1);
    const auto portrait = ht_selection_outline(0, 0, 600, 960, 1800, 1.5f, {}, gaps, 15);
    assert(portrait && portrait->x == 5.f && portrait->height < 960.f);
    const auto reduced = ht_selection_outline(0, 0, 600, 400, 1800, 1.5f, {}, gaps, 0);
    assert(reduced && reduced->radius >= 0 && reduced->radius < selected->radius);
    return 0;
}
