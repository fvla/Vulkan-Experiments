#pragma once

#include "vk_types.h"
#include "vk_sync.h"

#include <atomic>
#include <thread>
#include <vector>

template <typename T>
concept VulkanCommandRecorder = requires(T recorder)
{
    { recorder(std::declval<vk::CommandBuffer>()) } -> std::same_as<void>;
};

class VulkanCommandPool;

namespace detail
{

class VulkanCommandPoolImpl
{
    friend class VulkanCommandPool;

    vk::Device device_;
    size_t bufferCount_;
    vk::UniqueCommandPool commandPool_;
    std::vector<vk::UniqueCommandBuffer> commandBuffers_;

    VulkanCommandPoolImpl(vk::Device device, size_t bufferCount, VulkanQueueInfo queueInfo)
        : device_(device), bufferCount_(bufferCount)
    {
        const vk::CommandPoolCreateInfo commandPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueInfo.familyIndex);
        commandPool_ = device.createCommandPoolUnique(commandPoolInfo);

        const vk::CommandBufferAllocateInfo commandBufferInfo(*commandPool_, vk::CommandBufferLevel::ePrimary, gsl::narrow<uint32_t>(bufferCount));
        commandBuffers_ = device.allocateCommandBuffersUnique(commandBufferInfo);
    }

    vk::UniqueCommandBuffer checkOut()
    {
        if (commandBuffers_.empty())
        {
            const auto extraBuffers = bufferCount_ / 2;
            bufferCount_ += extraBuffers;
            const vk::CommandBufferAllocateInfo commandBufferInfo(*commandPool_, vk::CommandBufferLevel::ePrimary, gsl::narrow<uint32_t>(extraBuffers));
            auto newCommandBuffers = device_.allocateCommandBuffersUnique(commandBufferInfo);
            commandBuffers_.insert(
                commandBuffers_.end(),
                std::make_move_iterator(newCommandBuffers.begin()),
                std::make_move_iterator(newCommandBuffers.end())
            );
        }
        vk::UniqueCommandBuffer commandBuffer = std::move(commandBuffers_.back());
        commandBuffers_.pop_back();
        return commandBuffer;
    }
public:
    void checkIn(vk::UniqueCommandBuffer commandBuffer) noexcept
    {
        commandBuffers_.emplace_back();
        commandBuffers_.back().swap(commandBuffer);
    }
};

}

/* RAII wrapper for command buffer leased from a command pool. */
class VulkanCommandBuffer
{
    friend class VulkanCommandPool;

    std::shared_ptr<detail::VulkanCommandPoolImpl> commandPool_;
    vk::UniqueCommandBuffer commandBuffer_;
    VulkanFence<> fence_;

    template <VulkanCommandRecorder Recorder, vk::CommandBufferUsageFlags flags>
    void record_(Recorder recorder)
    {
        commandBuffer_->begin({ vk::CommandBufferUsageFlags(flags) });
        recorder(*commandBuffer_);
        commandBuffer_->end();
    }

    VulkanCommandBuffer(const vk::Device& device, std::shared_ptr<detail::VulkanCommandPoolImpl> commandPool, vk::UniqueCommandBuffer commandBuffer)
        : commandPool_(commandPool), commandBuffer_(std::move(commandBuffer)), fence_(device)
    {}
public:
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&) = default;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) = default;

    ~VulkanCommandBuffer()
    {
        /* Skip if command buffer was moved from */
        if (commandPool_)
        {
            try
            {
                VK_CHECK(wait());
                commandBuffer_->reset(vk::CommandBufferResetFlagBits::eReleaseResources);
            }
            catch (...) {}
            commandPool_->checkIn(std::move(commandBuffer_));
        }
    }

    void submitTo(const vk::Queue& queue, vk::SubmitInfo submitInfo = {})
    {
        queue.submit(submitInfo.setCommandBuffers(*commandBuffer_), fence_.get());
    }
    template <VulkanCommandRecorder Recorder>
    void record(Recorder recorder)
    {
        record_<Recorder, vk::CommandBufferUsageFlags{}>(recorder);
    }
    template <VulkanCommandRecorder Recorder>
    void record(const vk::RenderPassBeginInfo& renderPassInfo, Recorder recorder)
    {
        record([&](const vk::CommandBuffer& commandBuffer)
        {
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            recorder(commandBuffer);
            commandBuffer.endRenderPass();
        });
    }
    template <VulkanCommandRecorder Recorder>
    void recordOnce(Recorder recorder)
    {
        using enum vk::CommandBufferUsageFlagBits;
        record_<Recorder, eOneTimeSubmit>(recorder);
    }
    template <VulkanCommandRecorder Recorder>
    void recordOnce(const vk::RenderPassBeginInfo& renderPassInfo, Recorder recorder)
    {
        recordOnce([&](const vk::CommandBuffer& commandBuffer)
        {
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            recorder(commandBuffer);
            commandBuffer.endRenderPass();
        });
    }

    [[nodiscard]] vk::Result wait() const { return fence_.wait(); }
    [[nodiscard]] vk::Result wait(std::chrono::nanoseconds timeout) const { return fence_.wait(timeout); }
private:
    /* You never want to call reset without waiting first. */
    void reset() const { fence_.reset(); }
public:
    [[nodiscard]] vk::Result waitAndReset() const
    {
        const vk::Result result = wait();
        reset();
        return result;
    }
    [[nodiscard]] vk::Result waitAndReset(std::chrono::nanoseconds timeout) const
    {
        const vk::Result result = wait(timeout);
        reset();
        return result;
    }

    /* TODO: Stop breaking encapsulation! */
    const vk::CommandBuffer& get() const noexcept { return *commandBuffer_; }
};

/* Note: only handles primary command buffers. */
class VulkanCommandPool
{
    std::shared_ptr<detail::VulkanCommandPoolImpl> commandPoolImpl_;
public:
    VulkanCommandPool() noexcept {}
    [[gsl::suppress(r.11)]]
    VulkanCommandPool(const vk::Device& device, size_t bufferCount, VulkanQueueInfo queueInfo)
        : commandPoolImpl_(new detail::VulkanCommandPoolImpl(device, bufferCount, queueInfo))
    {}

    VulkanCommandBuffer checkOut()
    {
        Expects(commandPoolImpl_); // TODO: Remove this unsafe check by getting rid of default constructor.
        return { commandPoolImpl_->device_, commandPoolImpl_, commandPoolImpl_->checkOut() };
    }
};
