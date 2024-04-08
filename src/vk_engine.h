#pragma once

#include "vk_types.h"
#include "vk_device.h"
#include "vk_instance.h"

#include <memory>

struct SDL_Window;

class VulkanEngine
{
    vk::Extent2D windowExtent = { 1280, 720 };
    gsl::not_null<std::shared_ptr<SDL_Window>> window;
    gsl::not_null<std::shared_ptr<const VulkanInstance>> instance;
    gsl::not_null<std::shared_ptr<const VulkanDevice>> device;
public:
    VulkanEngine();
    DECLARE_CONSTRUCTORS_MOVE_DELETED(VulkanEngine)

    void run();
};
