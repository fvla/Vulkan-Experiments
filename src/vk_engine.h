#pragma once

#include "vk_types.h"
#include "vk_buffer.h"
#include "vk_command.h"
#include "vk_swapchain.h"

#include <memory>
#include <optional>

struct SDL_Window;

/* This class contains the main drawing logic. It's a mess; need to abstract it better
   once I understand the program structure better. This level of dependency injection
   is unacceptable. */
template <size_t bufferCount = 2, Void V = void>
class TrianglePipeline
{
    VulkanSemaphore<V> imageReadySemaphore_;
    VulkanSemaphore<V> renderFinishedSemaphore_;

    std::reference_wrapper<VulkanCommandHandlerPool<bufferCount>> commandHandlerPool_;
    OptionalReference<VulkanCommandHandler<bufferCount>> currentCommandHandler_;
    /* TODO: Move pipeline initialization into a new class "PipelineCore" instead of passing a reference. */
    std::reference_wrapper<const vk::Pipeline> pipeline_;
    /* TODO: viewports and scissors should be owned by the pipeline or pipeline core. */
    std::span<vk::Viewport> viewports_;
    std::span<vk::Rect2D> scissors_;

    VulkanCommandHandlerPool<5> temporaryCommandPool_;

    using enum vk::BufferUsageFlagBits;
    VulkanBuffer<eVertexBuffer | eTransferDst, VulkanBufferType::DeviceLocal> vertexBuffer_;

    constexpr static std::array<vk::PipelineStageFlags, 1> waitStages_
        = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    constexpr static std::array vertexBufferArray_ = {
        SimpleVertex{{ 0.0f,-0.5f, 0.0f},{1.0f,0.0f,0.0f}},
        SimpleVertex{{ 0.5f, 0.5f, 0.0f},{0.0f,1.0f,0.0f}},
        SimpleVertex{{-0.5f, 0.5f, 0.0f},{0.0f,0.0f,1.0f}},
    };
public:
    TrianglePipeline(const vk::PhysicalDevice& physicalDevice, const vk::Device& device,
                     VulkanCommandHandlerPool<bufferCount>& commandHandlerPool, const vk::Pipeline& pipeline,
                     const VulkanQueueInfo& queueInfo, std::span<vk::Viewport> viewports, std::span<vk::Rect2D> scissors) :
        imageReadySemaphore_(device), renderFinishedSemaphore_(device), commandHandlerPool_(commandHandlerPool),
        pipeline_(pipeline), viewports_(viewports), scissors_(scissors), temporaryCommandPool_(device, 1, queueInfo),
        vertexBuffer_(physicalDevice, device, sizeof(vertexBufferArray_))
    {
        VulkanBuffer<eTransferSrc, VulkanBufferType::Staging> stagingBuffer(physicalDevice, device, vertexBuffer_.size());
        stagingBuffer.copyFrom(std::span(vertexBufferArray_));
        {
            auto& handler = temporaryCommandPool_.getHandler(0);
            handler.recordOnce(recordCopyBuffers(stagingBuffer, vertexBuffer_));
            handler.submitTo(queueInfo.queue);
            handler.waitAndReset();
        }
    }
    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;
    TrianglePipeline(TrianglePipeline && other) = default;
    TrianglePipeline& operator=(TrianglePipeline&&) = default;
    ~TrianglePipeline()
    {
        currentCommandHandler_.apply([](auto& handler) { handler.waitAndReset(); });
    }

    void run(const VulkanSwapchain<>& swapchain, const vk::Queue& queue)
    {
        currentCommandHandler_.apply([](auto& handler) { handler.waitAndReset(); });
        uint32_t imageIndex = swapchain.acquireNextImage(imageReadySemaphore_.get());
        auto& handler = commandHandlerPool_.get().getHandler(imageIndex);
        currentCommandHandler_ = handler;

        {
            vk::SubmitInfo submitInfo(imageReadySemaphore_.get(), waitStages_, {}, renderFinishedSemaphore_.get());
            handler.submitTo(queue, submitInfo);
        }
        {
            vk::PresentInfoKHR presentInfo(renderFinishedSemaphore_.get(), swapchain.getSwapchain(), imageIndex);
            VK_CHECK(queue.presentKHR(presentInfo));
        }
    }

    void recordTriangleCommand(const vk::CommandBuffer& commandBuffer)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
        commandBuffer.setViewport(0, viewports_);
        commandBuffer.setScissor(0, scissors_);
        commandBuffer.bindVertexBuffers(0, vertexBuffer_.get(), vk::DeviceSize(0));
        commandBuffer.draw(static_cast<uint32_t>(vertexBuffer_.size()), 1, 0, 0);
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

    VulkanCommandHandlerPool<> commandHandlerPool_;

    std::vector<TrianglePipeline<>> trianglePipelines_;

    uint64_t frameNumber_{ 0 };

    SDL_Window* window_{ nullptr };

    vk::Extent2D windowExtent_{ 1600 , 1200 };
    std::array<vk::Viewport, 1> viewports_;
    std::array<vk::Rect2D, 1> scissors_;
    const std::vector<vk::ClearValue> clearValues_ = { vk::ClearColorValue(std::array{0.0f, 0.0f, 0.0f, 1.0f}) };
public:
    VulkanEngine();
    ~VulkanEngine();
    VulkanEngine(const VulkanEngine&) = delete;
    VulkanEngine& operator=(const VulkanEngine&) = delete;

    void recreateSwapchain();
    void draw();
    void run();
};
