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
concept ptr_type = std::same_as<T, std::string>;

template<ptr_type T>
inline T value(std::string config) {
    CConfigValue<T> val("plugin:hyprtasking:" + config);
    // const auto var = (T const *)Config::mgr()->getConfigValue("plugin:hyprtasking:" + config).dataptr;
    return *val;
    // static std::unordered_map<std::string, T const*> cache;
    //
    // if (!cache.count(config))
    //     cache[config] =
    //         (T const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:" + config)
    //             ->getDataStaticPtr();
    // return *cache[config];
}

template<typename T>
inline T value(std::string config)
    requires(!ptr_type<T>)
{
    CConfigValue<T> val("plugin:hyprtasking:" + config);
    // const auto var = (T const *)Config::mgr()->getConfigValue("plugin:hyprtasking:" + config).dataptr;
    return *val;
    // static std::unordered_map<std::string, T* const*> cache;
    //
    // if (!cache.count(config))
    //     cache[config] =
    //         (T* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:" + config)
    //             ->getDataStaticPtr();
    // return *cache[config];
}

} // namespace HTConfig
