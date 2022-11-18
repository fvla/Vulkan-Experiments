#pragma once

#include "vk_types.h"
#include "vk_sync.h"

#include <atomic>
#include <thread>
#include <vector>

template <typename T>
concept VulkanCommandRecorder = requires(T recorder)
{
    { recorder(vk::CommandBuffer()) } -> std::same_as<void>;
};

/* Command handler that uses ring buffer of command buffers to re-record commands.
   Not safe to use from multiple threads. */
template <int64_t bufferCount, Void V = void>
class VulkanCommandHandler
{
    static_assert(bufferCount > 1);
public:
    using CommandBuffersView = std::array<vk::CommandBuffer, bufferCount>; // Command buffers are owned by pool.
private:
    CommandBuffersView commandBuffers_;
    std::unique_ptr<std::array<std::atomic_bool, bufferCount>> commandBufferInFlight_ =
        std::make_unique<std::array<std::atomic_bool, bufferCount>>();
    std::array<VulkanFence<V>, bufferCount> commandBufferFences_;
    static constexpr int64_t terminateFlag_ = -2;
    std::unique_ptr<std::atomic<int64_t>> currentBufferIndex_ = std::make_unique<std::atomic<int64_t>>(-1);
    std::unique_ptr<std::atomic<int64_t>> currentDiscardedBufferIndex_ = std::make_unique<std::atomic<int64_t>>(-1);
    VulkanQueueInfo queueInfo_;

    /* Optimization would be one resetter thread per pool, rather than one per handler. */
    std::jthread resetter_;
    void commandResetThread()
    {
        /* Reminder: to ensure that the thread terminates, we must check if we are in termination state
           each time we load currentBufferIndex_. */
        auto index = currentBufferIndex_->load();
        while (index != terminateFlag_)
        {
            assert(currentDiscardedBufferIndex_->load() == index); // loop iteration pre-condition
            currentBufferIndex_->wait(index);
            index = currentBufferIndex_->load();
            if (index == terminateFlag_)
                break;
            assert(index != -1); // index == -1 only occurs at initial state
            assert(currentDiscardedBufferIndex_->load() != index);
            do
            {
                auto discardedIndex = currentDiscardedBufferIndex_->load();
                if (discardedIndex != -1)
                {
                    if (commandBufferInFlight_->data()[static_cast<size_t>(index)])
                        if (commandBufferFences_[static_cast<size_t>(discardedIndex)].wait() == vk::Result::eSuccess)
                            commandBufferFences_[static_cast<size_t>(discardedIndex)].reset();
                    commandBuffers_[static_cast<size_t>(discardedIndex)].reset(vk::CommandBufferResetFlagBits::eReleaseResources);
                }
                currentDiscardedBufferIndex_->store((discardedIndex + 1) / bufferCount);
                currentDiscardedBufferIndex_->notify_all();
                index = currentBufferIndex_->load();
            } while (index != terminateFlag_ && index != currentDiscardedBufferIndex_->load());
        }
    }

    template <size_t... Indices>
    std::array<VulkanFence<V>, bufferCount> getFences(const vk::Device& device, std::index_sequence<Indices...>)
    {
        static_assert(sizeof...(Indices) == bufferCount);
        return { (static_cast<void>(Indices), VulkanFence(device))... }; // Comma operator to ignore indices and construct bufferCount fences
    }
    template <typename IndexSequence>
    auto getFences(const vk::Device& device) { return getFences(device, IndexSequence()); }

    template <VulkanCommandRecorder Recorder, vk::CommandBufferUsageFlags flags>
    void record_(Recorder recorder)
    {
        int64_t nextBuffer = (currentBufferIndex_->load() + 1) % static_cast<int64_t>(bufferCount);
        currentDiscardedBufferIndex_->wait(nextBuffer); // Wait until nextBuffer is free (which should usually be instantly)
        currentBufferIndex_->store(nextBuffer);
        currentBufferIndex_->notify_all();

        const vk::CommandBuffer& currentBuffer = commandBuffers_[static_cast<size_t>(nextBuffer)];
        currentBuffer.begin({ vk::CommandBufferUsageFlags(flags) });
        recorder(currentBuffer);
        currentBuffer.end();
    }
public:
    VulkanCommandHandler(const vk::Device& device, CommandBuffersView commandBuffers) :
        commandBuffers_(std::move(commandBuffers)),
        commandBufferFences_(getFences<std::make_index_sequence<bufferCount>>(device))
    {
        resetter_ = std::jthread([this]() { commandResetThread(); });
    }
    ~VulkanCommandHandler() { currentBufferIndex_->store(terminateFlag_); currentBufferIndex_->notify_all(); }
    VulkanCommandHandler(const VulkanCommandHandler&) = delete;
    VulkanCommandHandler& operator=(const VulkanCommandHandler&) = delete;
    VulkanCommandHandler(VulkanCommandHandler&&) = default;
    VulkanCommandHandler& operator=(VulkanCommandHandler&&) = default;

    void submitTo(const vk::Queue& queue, vk::SubmitInfo submitInfo = {})
    {
        assert(currentBufferIndex_->load() >= 0);
        auto index = static_cast<uint64_t>(currentBufferIndex_->load());
        commandBufferInFlight_->data()[index].store(true);
        queue.submit(submitInfo.setCommandBuffers(commandBuffers_[index]), commandBufferFences_[index].get());
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
    void wait()
    {
        if (auto index = currentBufferIndex_->load(); index >= 0)
            commandBufferFences_[static_cast<size_t>(index)].wait();
    }
    void wait(std::chrono::nanoseconds timeout)
    {
        if (auto index = currentBufferIndex_->load(); index >= 0)
            commandBufferFences_[static_cast<size_t>(index)].wait(timeout);
    }
    void waitAndReset()
    {
        if (auto index = currentBufferIndex_->load(); index >= 0)
        {
            commandBufferFences_[static_cast<size_t>(index)].wait();
            /* Order matters; command buffer must be set to be not in flight before fence
               is reset to avoid deadlock. */
            commandBufferInFlight_->data()[static_cast<size_t>(index)].store(false);
            commandBufferFences_[static_cast<size_t>(index)].reset();
        }
    }
};

/* Note: only handles primary command buffers. */
template <size_t bufferCount = 2, Void V = void>
class VulkanCommandHandlerPool
{
    size_t handlerCount_;
    VulkanQueueInfo queueInfo_;
    vk::UniqueCommandPool commandPool_;
    std::vector<vk::UniqueCommandBuffer> commandBuffers_;

    using CommandHandler = VulkanCommandHandler<bufferCount, V>;
    std::vector<CommandHandler> commandHandlers_;

    template <size_t... Indices>
    CommandHandler::CommandBuffersView getCommandBuffersView(size_t handlerIndex, std::index_sequence<Indices...>)
    {
        static_assert(sizeof...(Indices) == bufferCount);
        return { commandBuffers_[Indices + handlerIndex * bufferCount].get()... };
    }
    template <typename IndexSequence>
    auto getCommandBuffersView(size_t handlerIndex) { return getCommandBuffersView(handlerIndex, IndexSequence()); }
public:
    VulkanCommandHandlerPool() = default;
    VulkanCommandHandlerPool(const VulkanCommandHandlerPool&) = delete;
    VulkanCommandHandlerPool& operator=(const VulkanCommandHandlerPool&) = delete;
    VulkanCommandHandlerPool(VulkanCommandHandlerPool&&) = default;
    VulkanCommandHandlerPool& operator=(VulkanCommandHandlerPool&&) = default;
    VulkanCommandHandlerPool(const vk::Device& device, size_t handlerCount, VulkanQueueInfo queueInfo)
        : handlerCount_(handlerCount), queueInfo_(queueInfo)
    {
        vk::CommandPoolCreateInfo commandPoolInfo({}, queueInfo_.familyIndex);
        commandPool_ = device.createCommandPoolUnique(commandPoolInfo);

        vk::CommandBufferAllocateInfo commandBufferInfo(*commandPool_, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(handlerCount * bufferCount));
        commandBuffers_ = device.allocateCommandBuffersUnique(commandBufferInfo);
        assert(commandBuffers_.size() == handlerCount * bufferCount);

        commandHandlers_.reserve(handlerCount);
        for (size_t i = 0; i < handlerCount; i++)
            commandHandlers_.emplace_back(device, getCommandBuffersView<std::make_index_sequence<bufferCount>>(i));
        assert(commandHandlers_.size() == handlerCount);
    }

    size_t getHandlerCount() const { return handlerCount_; }
    size_t size() const { return handlerCount_; }
    CommandHandler& getHandler(size_t index) { return commandHandlers_[index]; }
    auto begin() { return commandHandlers_.begin(); }
    auto end() { return commandHandlers_.end(); }
};
