#pragma once

#include "vk_types.h"
#include "vk_sync.h"

#include <atomic>
#include <thread>
#include <vector>

template <typename T>
concept VulkanCommandRecorder = requires(T recorder, vk::CommandBuffer commandBuffer)
{
    { recorder(commandBuffer) } -> std::same_as<void>;
};

class VulkanCommandPool;

namespace detail
{

class VulkanCommandPoolImpl
{
    friend class ::VulkanCommandPool;

    vk::Device device_;
    size_t bufferCount_;
    vk::UniqueCommandPool commandPool_;
    std::vector<vk::CommandBuffer> commandBuffers_;

    VulkanCommandPoolImpl(vk::Device device, size_t bufferCount, VulkanQueueInfo queueInfo)
        : device_(device), bufferCount_(bufferCount)
    {
        const vk::CommandPoolCreateInfo commandPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueInfo.familyIndex);
        commandPool_ = device.createCommandPoolUnique(commandPoolInfo);

        const vk::CommandBufferAllocateInfo commandBufferInfo(*commandPool_, vk::CommandBufferLevel::ePrimary, gsl::narrow<uint32_t>(bufferCount));
        commandBuffers_ = device.allocateCommandBuffers(commandBufferInfo);
    }
public:
    VulkanCommandPoolImpl(const VulkanCommandPoolImpl&) = delete;
    VulkanCommandPoolImpl& operator=(const VulkanCommandPoolImpl&) = delete;
    VulkanCommandPoolImpl(VulkanCommandPoolImpl&&) = default;
    VulkanCommandPoolImpl& operator=(VulkanCommandPoolImpl&&) = default;

    ~VulkanCommandPoolImpl()
    {
        device_.freeCommandBuffers(*commandPool_, commandBuffers_);
    }
private:
    vk::CommandBuffer checkOut()
    {
        if (commandBuffers_.empty())
        {
            const auto extraBuffers = bufferCount_ / 2;
            bufferCount_ += extraBuffers;
            const vk::CommandBufferAllocateInfo commandBufferInfo(*commandPool_, vk::CommandBufferLevel::ePrimary, gsl::narrow<uint32_t>(extraBuffers));
            auto newCommandBuffers = device_.allocateCommandBuffers(commandBufferInfo);
            commandBuffers_.insert(
                commandBuffers_.end(),
                std::make_move_iterator(newCommandBuffers.begin()),
                std::make_move_iterator(newCommandBuffers.end())
            );
        }
        vk::CommandBuffer commandBuffer = commandBuffers_.back();
        commandBuffers_.pop_back();
        return commandBuffer;
    }
public:
    void checkIn(vk::CommandBuffer commandBuffer) noexcept
    {
        commandBuffers_.push_back(commandBuffer);
    }
};

}

/* RAII wrapper for command buffer leased from a command pool. */
class VulkanCommandBuffer
{
    friend class VulkanCommandPool;

    std::shared_ptr<detail::VulkanCommandPoolImpl> commandPool_;
    vk::CommandBuffer commandBuffer_;
    VulkanFence fence_;
    bool submitted_ = false;

    template <vk::CommandBufferUsageFlags flags>
    void record_(VulkanCommandRecorder auto& recorder)
    {
        commandBuffer_.begin({ vk::CommandBufferUsageFlags(flags) });
        recorder(commandBuffer_);
        commandBuffer_.end();
    }

    VulkanCommandBuffer(vk::Device device, std::shared_ptr<detail::VulkanCommandPoolImpl> commandPool, vk::CommandBuffer commandBuffer)
        : commandPool_(commandPool), commandBuffer_(std::move(commandBuffer)), fence_(device)
    {}
public:
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&) = default;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) = default;

    ~VulkanCommandBuffer()
    {
        /* Skip destructor if command buffer was moved from */
        if (!commandPool_)
            return;
        try
        {
            std::ignore = waitAndReset();
            commandBuffer_.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        }
        catch (...) {}
        commandPool_->checkIn(commandBuffer_);
    }

    void submitTo(const vk::Queue& queue, vk::SubmitInfo submitInfo)
    {
        submitted_ = true;
        queue.submit(submitInfo.setCommandBuffers(commandBuffer_), fence_.get());
    }
    void record(VulkanCommandRecorder auto& recorder)
    {
        record_<vk::CommandBufferUsageFlags{}>(recorder);
    }
    void record(const vk::RenderPassBeginInfo& renderPassInfo, VulkanCommandRecorder auto& recorder)
    {
        record([&renderPassInfo, &recorder](const vk::CommandBuffer& commandBuffer)
        {
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            recorder(commandBuffer);
            commandBuffer.endRenderPass();
        });
    }
    void recordOnce(VulkanCommandRecorder auto& recorder)
    {
        using enum vk::CommandBufferUsageFlagBits;
        record_<eOneTimeSubmit>(recorder);
    }
    void recordOnce(const vk::RenderPassBeginInfo& renderPassInfo, VulkanCommandRecorder auto& recorder)
    {
        recordOnce([&renderPassInfo, &recorder](const vk::CommandBuffer& commandBuffer)
        {
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            recorder(commandBuffer);
            commandBuffer.endRenderPass();
        });
    }

    [[nodiscard]] vk::Result wait() const { return submitted_ ? fence_.wait() : vk::Result::eSuccess; }
    [[nodiscard]] vk::Result wait(std::chrono::nanoseconds timeout) const { return submitted_ ? fence_.wait(timeout) : vk::Result::eSuccess; }
private:
    void reset()
    {
        if (fence_.status() == vk::Result::eSuccess)
        {
            fence_.reset();
            submitted_ = false;
        }
    }
public:
    [[nodiscard]] vk::Result waitAndReset()
    {
        const vk::Result result = wait();
        reset();
        return result;
    }
    [[nodiscard]] vk::Result waitAndReset(std::chrono::nanoseconds timeout)
    {
        const vk::Result result = wait(timeout);
        reset();
        return result;
    }
};

/* Note: only handles primary command buffers. */
class VulkanCommandPool
{
    static_assert(std::_Can_scalar_delete<detail::VulkanCommandPoolImpl>::value, "Cannot delete pool impl");
    std::shared_ptr<detail::VulkanCommandPoolImpl> commandPoolImpl_;
public:
    GSL_SUPPRESS(r.11)
    VulkanCommandPool(vk::Device device, size_t bufferCount, VulkanQueueInfo queueInfo)
        : commandPoolImpl_(new detail::VulkanCommandPoolImpl(device, bufferCount, queueInfo))
    {}

    VulkanCommandBuffer checkOut()
    {
        Expects(commandPoolImpl_); // TODO: Remove this unsafe check by getting rid of default constructor.
        return VulkanCommandBuffer(commandPoolImpl_->device_, commandPoolImpl_, commandPoolImpl_->checkOut());
    }
};
