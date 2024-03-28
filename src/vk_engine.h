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

struct VertexPushConstants
{
    glm::mat4 renderMatrix;
};

void runEngine();
