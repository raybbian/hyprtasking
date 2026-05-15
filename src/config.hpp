#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"

using namespace Config::Values;

namespace HTConfig {

template<typename T>
inline T value(std::string config) {
    static std::unordered_map<std::string, CConfigValue<T>> cache;

    if (!cache.count(config)) {
        const CConfigValue<T> val("plugin:hyprtasking:" + config);
        cache[config] = val;
    }

    return *cache[config];
}

} // namespace HTConfig
