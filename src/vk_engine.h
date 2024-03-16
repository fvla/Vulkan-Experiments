#pragma once

#include "vk_types.h"
#include "vk_buffer.h"
#include "vk_command.h"
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

/* This class contains the main drawing logic. It's a mess; need to abstract it better
   once I understand the program structure better. This level of dependency injection
   is unacceptable. */
class TrianglePipeline
{
    VulkanGraphicsStream stream_;

    using enum vk::BufferUsageFlagBits;
    VulkanBuffer<eVertexBuffer | eTransferDst, VulkanBufferType::DeviceLocal> vertexBuffer_;

    /* TODO: Move pipeline initialization into a new class "PipelineCore" instead of passing a reference. */
    const vk::PipelineLayout& pipelineLayout_;
    const vk::Pipeline& pipeline_;
    /* TODO: viewports and scissors should be owned by the pipeline or pipeline core. */
    gsl::span<vk::Viewport> viewports_;
    gsl::span<vk::Rect2D> scissors_;

    constexpr static std::array vertexBufferArray_ = std::to_array<SimpleVertex>({
        {{ 0.0f,-0.5f, 0.0f},{1.0f,0.0f,0.0f}},
        {{ 0.5f, 0.5f, 0.0f},{0.0f,1.0f,0.0f}},
        {{-0.5f, 0.5f, 0.0f},{0.0f,0.0f,1.0f}},
    });

    auto triangleCommandRecorder(uint64_t frameNumber)
    {
        return [&, frameNumber](const vk::CommandBuffer& commandBuffer)
        {
            glm::vec3 cameraPosition = { 0.0f,-0.1f,-2.0f };

            glm::mat4 view = glm::translate(glm::mat4(1.f), cameraPosition);
            glm::mat4 projection = glm::perspective(glm::radians(90.f), viewports_[0].width / viewports_[0].height, 0.1f, 20.0f);
            glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, glm::radians(frameNumber * 2.0f), glm::vec3(0, 1, 0));

            VertexPushConstants constants;
            constants.renderMatrix = projection * view * model;
            using enum vk::ShaderStageFlagBits;
            commandBuffer.pushConstants<VertexPushConstants>(pipelineLayout_, eVertex, 0, constants);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
            commandBuffer.setViewport(0, viewports_);
            commandBuffer.setScissor(0, scissors_);
            commandBuffer.bindVertexBuffers(0, vertexBuffer_.get(), vk::DeviceSize{ 0 });
            commandBuffer.draw(gsl::narrow<uint32_t>(vertexBuffer_.size()), 1, 0, 0);
        };
    }
public:
    TrianglePipeline(const vk::PhysicalDevice& physicalDevice, const vk::Device& device,
                     VulkanCommandPool& commandPool, const vk::PipelineLayout& pipelineLayout, const vk::Pipeline& pipeline,
                     const VulkanQueueInfo& queueInfo, gsl::span<vk::Viewport> viewports, gsl::span<vk::Rect2D> scissors) :
        stream_(device, commandPool), vertexBuffer_(physicalDevice, device, sizeof(vertexBufferArray_)),
        pipelineLayout_(pipelineLayout), pipeline_(pipeline), viewports_(viewports), scissors_(scissors)
    {
        const VulkanBuffer<eTransferSrc, VulkanBufferType::Staging> stagingBuffer(physicalDevice, device, vertexBuffer_.size());
        stagingBuffer.copyFrom(gsl::span(vertexBufferArray_));
        {
            auto commandRecorder = recordCopyBuffers(stagingBuffer, vertexBuffer_);
            stream_.submitWork(queueInfo.queue, commandRecorder);
        }
    }
    ~TrianglePipeline() { stream_.synchronize(); }

    void run(const VulkanSwapchain& swapchain, const vk::Queue& queue, const vk::RenderPassBeginInfo& renderPassInfo, uint64_t frameNumber)
    {
        stream_.synchronize();
        const uint32_t imageIndex = stream_.acquireNextImage(queue, swapchain);
        auto commandRecorder = triangleCommandRecorder(frameNumber);
        stream_.submitWork(queue, renderPassInfo, commandRecorder);
        stream_.present(queue, swapchain, imageIndex);
    }
};

class VulkanEngine
{
    vk::UniqueInstance instance_; // Vulkan library handle
    vk::DebugUtilsMessengerEXT debug_messenger_; // Vulkan debug output handle
    vk::PhysicalDevice physicalDevice_; // GPU chosen as the default device
    vk::UniqueDevice device_; // Vulkan device for commands
    std::vector<VulkanQueueInfo> graphicsQueues_; // All graphics device queues

    vk::UniqueSurfaceKHR surface_;
    vk::SurfaceFormatKHR surfaceFormat_;

    std::optional<VulkanSwapchain> swapchain_;

    vk::UniqueRenderPass renderPass_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;

    vk::UniqueBuffer vertexBuffer_;

    std::optional<VulkanCommandPool> commandPool_;

    std::deque<TrianglePipeline> trianglePipelines_;

    uint64_t frameNumber_{ 0 };

    SDL_Window* window_{ nullptr };

    vk::Extent2D windowExtent_{ 1280 , 720 };
    std::array<vk::Viewport, 1> viewports_;
    std::array<vk::Rect2D, 1> scissors_;
    const std::vector<vk::ClearValue> clearValues_ = { vk::ClearColorValue(std::array{0.0f, 0.0f, 0.0f, 1.0f}) };
public:
    VulkanEngine();
    ~VulkanEngine();
    VulkanEngine(const VulkanEngine&) = delete;
    VulkanEngine& operator=(const VulkanEngine&) = delete;
    VulkanEngine(VulkanEngine&&) = delete;
    VulkanEngine& operator=(VulkanEngine&&) = delete;

    void resetSurface();
    void recreateSwapchain();
    void draw();
    void run();
};
