#pragma once

#include "vk_types.h"
#include "vk_swapchain.h"
#include "vk_sync.h"
#include "vk_command.h"

#include <queue>
#include <ranges>
#include <thread>
#include <vector>

class VulkanStream;
class VulkanGraphicsStream;

class VulkanStreamEvent
{
    friend class VulkanStream;
    friend class VulkanGraphicsStream;

    const VulkanStream& stream_;
    uint64_t timelineValue_;

    inline static vk::Semaphore getSemaphore(const VulkanStreamEvent& e) noexcept;
    static uint64_t getSemaphoreValue(const VulkanStreamEvent& e) noexcept { return e.timelineValue_; }

    VulkanStreamEvent(const VulkanStream& stream, uint64_t timelineValue) noexcept
        : stream_(stream), timelineValue_(timelineValue) {}
public:
    template <std::same_as<VulkanStreamEvent> ...Events>
    static inline void submitEvents(const vk::Queue& queue, const VulkanStreamEvent& signalEvent, const Events&... waitEvents);

    inline void synchronize() const;
};

class VulkanStream
{
    friend class VulkanStreamEvent;
    friend class VulkanGraphicsStream;

    std::shared_ptr<VulkanCommandPool> commandPool_;
    std::optional<VulkanCommandBuffer> currentCommandBuffer_;
    VulkanTimelineSemaphore semaphore_;
    uint64_t lastValue_ = 0;
public:
    VulkanStream(const vk::Device& device, std::shared_ptr<VulkanCommandPool> commandPool)
        : commandPool_(commandPool), semaphore_(device) {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanStream)

    virtual ~VulkanStream()
    {
        try
        {
            synchronize();
        }
        catch (const vk::SystemError& e) {}
    }

    VulkanStreamEvent getLastEvent() const noexcept
    {
        return VulkanStreamEvent(*this, lastValue_);
    }

    void submitWork(const vk::Queue& queue, VulkanCommandRecorder auto& recorder, const vk::ArrayProxy<VulkanStreamEvent>& waitEvents = {})
    {
        auto waitSemaphoreValues = std::views::transform(waitEvents, VulkanStreamEvent::getSemaphoreValue) | std::ranges::to<std::vector>();
        auto waitSemaphores      = std::views::transform(waitEvents, VulkanStreamEvent::getSemaphore)      | std::ranges::to<std::vector>();
        waitSemaphoreValues.push_back(lastValue_);
        waitSemaphores.push_back(semaphore_.get());

        const vk::TimelineSemaphoreSubmitInfo timelineSubmit(waitSemaphoreValues, ++lastValue_);
        currentCommandBuffer_ = commandPool_->checkOut();
        currentCommandBuffer_->recordOnce(recorder);
        const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        const vk::SubmitInfo submitInfo(waitSemaphores, waitStage, {}, semaphore_.get(), &timelineSubmit);
        currentCommandBuffer_->submitTo(queue, submitInfo);
    }
    
    void synchronize() const
    {
        semaphore_.wait(lastValue_);
    }
};

class VulkanGraphicsStream : public VulkanStream
{
private:
    VulkanSemaphore acquireSemaphore_;
    VulkanSemaphore presentSemaphore_;
public:
    VulkanGraphicsStream(const vk::Device& device, std::shared_ptr<VulkanCommandPool> commandPool)
        : VulkanStream(device, commandPool), acquireSemaphore_(device), presentSemaphore_(device) {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanGraphicsStream)

    using VulkanStream::submitWork;

    void submitWork(const vk::Queue& queue, const vk::RenderPassBeginInfo& renderPassInfo, VulkanCommandRecorder auto& recorder,
                    const vk::ArrayProxy<VulkanStreamEvent>& waitEvents = {})
    {
        auto wrappedRecorder = [&renderPassInfo, &recorder](const vk::CommandBuffer& commandBuffer)
        {
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            recorder(commandBuffer);
            commandBuffer.endRenderPass();
        };
        submitWork(queue, wrappedRecorder, waitEvents);
    }

    /* TODO: Don't return raw, unencapsulated uint32_t to be later received by present(). */
    uint32_t acquireNextImage(const vk::Queue& queue, const VulkanSwapchain& swapchain)
    {
        const uint32_t imageIndex = swapchain.acquireNextImage(acquireSemaphore_.get());

        constexpr uint64_t acquireSemaphoreValue = std::numeric_limits<uint64_t>::max(); // will be ignored since acquireSemaphore isn't timeline
        const vk::TimelineSemaphoreSubmitInfo timelineSubmit(acquireSemaphoreValue, ++lastValue_);
        const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eNone;
        queue.submit(vk::SubmitInfo{ acquireSemaphore_.get(), waitStage, {}, semaphore_.get(), &timelineSubmit });

        return imageIndex;
    }
    
    void present(const vk::Queue& queue, const VulkanSwapchain& swapchain, uint32_t imageIndex)
    {
        constexpr uint64_t presentSemaphoreValue = std::numeric_limits<uint64_t>::max(); // will be ignored since presentSemaphore isn't timeline
        const vk::TimelineSemaphoreSubmitInfo timelineSubmit(lastValue_, presentSemaphoreValue);
        const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eNone;
        queue.submit(vk::SubmitInfo{ semaphore_.get(), waitStage, {}, presentSemaphore_.get(), &timelineSubmit });

        const vk::PresentInfoKHR presentInfo(presentSemaphore_.get(), *swapchain.getSwapchain(), imageIndex, {});
        VK_CHECK(queue.presentKHR(presentInfo));
    }
};

vk::Semaphore VulkanStreamEvent::getSemaphore(const VulkanStreamEvent& e) noexcept { return e.stream_.semaphore_.get(); }

template <std::same_as<VulkanStreamEvent> ...Events>
void VulkanStreamEvent::submitEvents(const vk::Queue& queue, const VulkanStreamEvent& signalEvent, const Events&... waitEvents)
{
    vk::TimelineSemaphoreSubmitInfo timelineSubmit(
        { waitEvents.timelineValue_... },
        signalEvent.timelineValue_ + 1
    );
    vk::SubmitInfo submitInfo({ waitEvents.stream_.semaphore_... }, {}, signalEvent.stream_.semaphore_, timelineSubmit);
    queue.submit(submitInfo);
}

void VulkanStreamEvent::synchronize() const
{
    stream_.semaphore_.wait(timelineValue_);
}
