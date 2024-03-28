#pragma once

#include "vk_types.h"
#include "vk_buffer.h"
#include "vk_command.h"
#include "vk_instance.h"
#include "vk_stream.h"
#include "vk_swapchain.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <functional>
#include <memory>
#include <optional>

struct SDL_Window;

class VulkanEngine
{
    vk::Extent2D windowExtent = { 1280, 720 };
    gsl::not_null<std::shared_ptr<SDL_Window>> window;
    gsl::not_null<std::shared_ptr<const VulkanInstance>> instance;
    gsl::not_null<std::shared_ptr<const VulkanDevice>> device;
public:
    VulkanEngine();

    void run();
};
