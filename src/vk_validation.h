#pragma once

#include "vk_types.h"

#include <sstream>
#include <unordered_set>
#include <vector>

inline void checkValidationLayers(gsl::span<const char* const> validationLayers)
{
    if constexpr (vk::enableValidationLayers)
    {
        auto availableLayers_vector = vk::enumerateInstanceLayerProperties();
        std::unordered_set<std::string> availableLayers;
        for (const auto& layer : availableLayers_vector)
            availableLayers.insert(std::string(layer.layerName.data()));
        auto badLayers =
            validationLayers
            | std::views::filter([&availableLayers](const char* str) { return !availableLayers.contains(str); });
        if (!badLayers.empty())
        {
            std::stringstream ss("The following validation layers are not available: ");
            for (const char* str : badLayers)
                ss << str << ", ";
            auto errorMessage = ss.str();
            errorMessage.erase(errorMessage.length() - 2);
            throw FatalError(errorMessage);
        }
    }
}
