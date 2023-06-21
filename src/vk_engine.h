#pragma once

#include "vk_types.h"
#include "vk_buffer.h"
#include "vk_command.h"
#include "vk_swapchain.h"

#include <functional>
#include <memory>
#include <optional>

struct SDL_Window;

/* This class contains the main drawing logic. It's a mess; need to abstract it better
   once I understand the program structure better. This level of dependency injection
   is unacceptable. */
class TrianglePipeline
{
    VulkanSemaphore<> imageReadySemaphore_;
    VulkanSemaphore<> renderFinishedSemaphore_;

    using enum vk::BufferUsageFlagBits;
    VulkanBuffer<eVertexBuffer | eTransferDst, VulkanBufferType::DeviceLocal> vertexBuffer_;

    VulkanCommandPool& commandPool_;
    VulkanCommandBuffer commandBuffer_;
    /* TODO: Move pipeline initialization into a new class "PipelineCore" instead of passing a reference. */
    const vk::Pipeline& pipeline_;
    /* TODO: viewports and scissors should be owned by the pipeline or pipeline core. */
    gsl::span<vk::Viewport> viewports_;
    gsl::span<vk::Rect2D> scissors_;

    constexpr static std::array<vk::PipelineStageFlags, 1> waitStages_
        = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    constexpr static std::array vertexBufferArray_ = std::to_array<SimpleVertex>({
        {{ 0.0f,-0.5f, 0.0f},{1.0f,0.0f,0.0f}},
        {{ 0.5f, 0.5f, 0.0f},{0.0f,1.0f,0.0f}},
        {{-0.5f, 0.5f, 0.0f},{0.0f,0.0f,1.0f}},
    });
public:
    TrianglePipeline(const vk::PhysicalDevice& physicalDevice, const vk::Device& device,
                     VulkanCommandPool& commandPool, const vk::Pipeline& pipeline,
                     const VulkanQueueInfo& queueInfo, gsl::span<vk::Viewport> viewports, gsl::span<vk::Rect2D> scissors) :
        imageReadySemaphore_(device), renderFinishedSemaphore_(device),
        vertexBuffer_(physicalDevice, device, sizeof(vertexBufferArray_)), commandPool_(commandPool),
        commandBuffer_(commandPool.checkOut()), pipeline_(pipeline), viewports_(viewports), scissors_(scissors)
    {
        VulkanBuffer<eTransferSrc, VulkanBufferType::Staging> stagingBuffer(physicalDevice, device, vertexBuffer_.size());
        stagingBuffer.copyFrom(gsl::span(vertexBufferArray_));
        {
            auto commandBuffer = commandPool_.checkOut();
            commandBuffer.recordOnce(recordCopyBuffers(stagingBuffer, vertexBuffer_));
            commandBuffer.submitTo(queueInfo.queue);
        }
    }
    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;
    TrianglePipeline(TrianglePipeline&& other) = default;
    TrianglePipeline& operator=(TrianglePipeline&&) = default;

    void run(const VulkanSwapchain<>& swapchain, const vk::Queue& queue)
    {
        const uint32_t imageIndex = swapchain.acquireNextImage(imageReadySemaphore_.get());
        VK_CHECK(commandBuffer_.waitAndReset());

        {
            const vk::SubmitInfo submitInfo(imageReadySemaphore_.get(), waitStages_, {}, renderFinishedSemaphore_.get());
            commandBuffer_.submitTo(queue, submitInfo);
        }
        {
            const vk::PresentInfoKHR presentInfo(renderFinishedSemaphore_.get(), swapchain.getSwapchain(), imageIndex);
            VK_CHECK(queue.presentKHR(presentInfo));
        }
    }

    void record(const vk::RenderPassBeginInfo& renderPassInfo)
    {
        commandBuffer_.record(renderPassInfo,
                              [this](const auto& commandBuffer) { recordTriangleCommand(commandBuffer); });
    }
private:
    void recordTriangleCommand(const vk::CommandBuffer& commandBuffer)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
        commandBuffer.setViewport(0, viewports_);
        commandBuffer.setScissor(0, scissors_);
        commandBuffer.bindVertexBuffers(0, vertexBuffer_.get(), vk::DeviceSize{0});
        commandBuffer.draw(gsl::narrow<uint32_t>(vertexBuffer_.size()), 1, 0, 0);
    }
};

class VulkanEngine : RequiresFeature<SDLFeature>
{
    vk::UniqueInstance instance_; // Vulkan library handle
    vk::DebugUtilsMessengerEXT debug_messenger_; // Vulkan debug output handle
    vk::PhysicalDevice physicalDevice_; // GPU chosen as the default device
    vk::UniqueDevice device_; // Vulkan device for commands
    std::vector<VulkanQueueInfo> graphicsQueues_; // All graphics device queues

    vk::UniqueSurfaceKHR surface_;
    vk::SurfaceFormatKHR surfaceFormat_;

    std::optional<VulkanSwapchain<>> swapchain_;

    vk::UniqueRenderPass renderPass_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;

    vk::UniqueBuffer vertexBuffer_;

    VulkanCommandPool commandPool_;

    std::vector<TrianglePipeline> trianglePipelines_;

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
