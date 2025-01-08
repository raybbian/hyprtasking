#include "config.hpp"

#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"

std::string HTConfig::layout() {
    static auto const* PLAYOUT =
        (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:layout")
            ->getDataStaticPtr();
    return *PLAYOUT;
}

int HTConfig::bg_color() {
    static long* const* PBG_COLOR =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:bg_color")
            ->getDataStaticPtr();
    return **PBG_COLOR;
}

int HTConfig::gap_size() {
    static long* const* PGAP_SIZE =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:gap_size")
            ->getDataStaticPtr();
    return **PGAP_SIZE;
}

int HTConfig::grid_rows() {
    static long* const* PROWS =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:grid:rows")
            ->getDataStaticPtr();
    return **PROWS;
}

int HTConfig::border_size() {
    static long* const* PBORDER_SIZE =
        (Hyprlang::INT* const*)
            HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:border_size")
                ->getDataStaticPtr();
    return **PBORDER_SIZE;
}

int HTConfig::linear_height() {
    static long* const* PLINEAR_HEIGHT =
        (Hyprlang::INT* const*)
            HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:linear:height")
                ->getDataStaticPtr();
    return **PLINEAR_HEIGHT;
}

std::string HTConfig::exit_behavior() {
    static auto const* PEXIT_BEHAVIOR =
        (Hyprlang::STRING const*)
            HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:exit_behavior")
                ->getDataStaticPtr();
    return *PEXIT_BEHAVIOR;
}
