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

/* This class contains the main drawing logic. It's a mess; need to abstract it better
   once I understand the program structure better. This level of dependency injection
   is unacceptable. */
//class TrianglePipeline
//{
//    VulkanGraphicsStream stream_;
//
//    using enum vk::BufferUsageFlagBits;
//    VulkanBuffer<eVertexBuffer | eTransferDst, VulkanBufferType::DeviceLocal> vertexBuffer_;
//
//    /* TODO: Move pipeline initialization into a new class "PipelineCore" instead of passing a reference. */
//    const vk::PipelineLayout& pipelineLayout_;
//    const vk::Pipeline& pipeline_;
//    /* TODO: viewports and scissors should be owned by the pipeline or pipeline core. */
//    gsl::span<vk::Viewport> viewports_;
//    gsl::span<vk::Rect2D> scissors_;
//
//    constexpr static std::array vertexBufferArray_ = std::to_array<SimpleVertex>({
//        {{ 0.0f,-0.5f, 0.0f},{1.0f,0.0f,0.0f}},
//        {{ 0.5f, 0.5f, 0.0f},{0.0f,1.0f,0.0f}},
//        {{-0.5f, 0.5f, 0.0f},{0.0f,0.0f,1.0f}},
//    });
//
//    auto triangleCommandRecorder(uint64_t frameNumber) noexcept
//    {
//        return [&, frameNumber](const vk::CommandBuffer& commandBuffer)
//        {
//            const glm::vec3 cameraPosition = { 0.0f,-0.1f,-2.0f };
//
//            const glm::mat4 view = glm::translate(glm::mat4(1.f), cameraPosition);
//            const glm::mat4 projection = glm::perspective(glm::radians(90.f), viewports_[0].width / viewports_[0].height, 0.1f, 20.0f);
//            const glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, glm::radians(frameNumber * 2.0f), glm::vec3(0, 1, 0));
//
//            VertexPushConstants constants;
//            constants.renderMatrix = projection * view * model;
//            using enum vk::ShaderStageFlagBits;
//            commandBuffer.pushConstants<VertexPushConstants>(pipelineLayout_, eVertex, 0, constants);
//
//            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
//            commandBuffer.setViewport(0, viewports_);
//            commandBuffer.setScissor(0, scissors_);
//            commandBuffer.bindVertexBuffers(0, vertexBuffer_.get(), vk::DeviceSize{ 0 });
//            commandBuffer.draw(gsl::narrow<uint32_t>(vertexBuffer_.size()), 1, 0, 0);
//        };
//    }
//public:
//    TrianglePipeline(const vk::PhysicalDevice& physicalDevice, const vk::Device& device,
//                     VulkanCommandPool& commandPool, const vk::PipelineLayout& pipelineLayout, const vk::Pipeline& pipeline,
//                     const VulkanQueueInfo& queueInfo, gsl::span<vk::Viewport> viewports, gsl::span<vk::Rect2D> scissors) :
//        stream_(device, commandPool), vertexBuffer_(physicalDevice, device, sizeof(vertexBufferArray_)),
//        pipelineLayout_(pipelineLayout), pipeline_(pipeline), viewports_(viewports), scissors_(scissors)
//    {
//        const VulkanBuffer<eTransferSrc, VulkanBufferType::Staging> stagingBuffer(physicalDevice, device, vertexBuffer_.size());
//        stagingBuffer.copyFrom(gsl::span(vertexBufferArray_));
//        {
//            auto commandRecorder = recordCopyBuffers(stagingBuffer, vertexBuffer_);
//            stream_.submitWork(queueInfo.queue, commandRecorder);
//        }
//    }
//    TrianglePipeline(const TrianglePipeline&) = delete;
//    TrianglePipeline& operator=(const TrianglePipeline&) = delete;
//    TrianglePipeline(TrianglePipeline&&) = default;
//    TrianglePipeline& operator=(TrianglePipeline&&) = default;
//
//    ~TrianglePipeline() { stream_.synchronize(); }
//
//    void run(const VulkanSwapchain& swapchain, const vk::Queue& queue, const vk::RenderPassBeginInfo& renderPassInfo, uint64_t frameNumber)
//    {
//        stream_.synchronize();
//        const uint32_t imageIndex = stream_.acquireNextImage(queue, swapchain);
//        auto commandRecorder = triangleCommandRecorder(frameNumber);
//        stream_.submitWork(queue, renderPassInfo, commandRecorder);
//        stream_.present(queue, swapchain, imageIndex);
//    }
//};
