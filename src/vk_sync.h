#pragma once

#include "vk_types.h"

#include <chrono>
#include <limits>

template <Void V = void>
class VulkanSemaphore : RequiresFeature<SemaphoreFeature, V>
{
private:
    vk::UniqueSemaphore semaphore_;
public:
    VulkanSemaphore(const vk::Device& device)
        : semaphore_(device.createSemaphoreUnique({}))
    {}
    VulkanSemaphore(const VulkanSemaphore&) = delete;
    VulkanSemaphore& operator=(const VulkanSemaphore&) = delete;
    VulkanSemaphore(VulkanSemaphore&&) = default;
    VulkanSemaphore& operator=(VulkanSemaphore&&) = default;

    const vk::Semaphore& get() const noexcept { return semaphore_.get(); }
};

template <Void V = void>
class VulkanTimelineSemaphore : RequiresFeature<TimelineSemaphoreFeature, V>
{
private:
    vk::Device device_;
    vk::UniqueSemaphore semaphore_;
    constexpr static auto semaphoreTypeInfo_ = vk::SemaphoreTypeCreateInfo(vk::SemaphoreType::eTimeline);

    vk::Result wait(uint64_t timeout) const { return device_.waitSemaphores({ {}, *semaphore_ }, timeout); }
public:
    VulkanTimelineSemaphore(const vk::Device& device) :
        device_(device), semaphore_(device.createSemaphoreUnique({ {}, &semaphoreTypeInfo_ }))
    {}
    VulkanTimelineSemaphore(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore& operator=(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore(VulkanTimelineSemaphore&&) = default;
    VulkanTimelineSemaphore& operator=(VulkanTimelineSemaphore&&) = default;

    const vk::Semaphore& get() const noexcept { return semaphore_.get(); }
    void wait(std::chrono::nanoseconds timeout) const { wait(gsl::narrow_cast<uint64_t>(std::max(0ll, timeout.count()))); }
    void wait() const { wait(std::chrono::nanoseconds::max()); }
    uint64_t counter() const { device_.getSemaphoreCounterValue(*semaphore_); }
    void signal(uint64_t value) const { device_.signalSemaphore({ *semaphore_, value }); }
};

template <Void V = void>
class VulkanFence : RequiresFeature<FenceFeature, V>
{
private:
    vk::Device device_;
    vk::UniqueFence fence_;

    vk::Result wait(uint64_t timeout) const { return device_.waitForFences({ *fence_ }, true, timeout); }
public:
    VulkanFence(const vk::Device& device)
        : device_(device), fence_(device.createFenceUnique({}))
    {}
    VulkanFence(const VulkanFence&) = delete;
    VulkanFence& operator=(const VulkanFence&) = delete;
    VulkanFence(VulkanFence&&) = default;
    VulkanFence& operator=(VulkanFence&&) = default;

    const vk::Fence& get() const noexcept { return fence_.get(); }
    vk::Result wait(std::chrono::nanoseconds timeout) const { return wait(gsl::narrow_cast<uint64_t>(std::max(0ll, timeout.count()))); }
    vk::Result wait() const { return wait(std::chrono::nanoseconds::max()); }
    void reset() const { device_.resetFences({ *fence_ }); }
    /* Possible values are eSuccess, eNotReady, and eErrorDeviceLost */
    vk::Result status() const { device_.getFenceStatus(*fence_); }
};
