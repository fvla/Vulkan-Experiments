#pragma once

#include "vk_types.h"

#include <chrono>
#include <limits>

class VulkanSemaphore
{
private:
    vk::UniqueSemaphore semaphore_;
public:
    VulkanSemaphore(const vk::Device& device)
        : semaphore_(device.createSemaphoreUnique({}))
    {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanSemaphore);

    const vk::Semaphore& get() const noexcept { return semaphore_.get(); }
};

class VulkanTimelineSemaphore
{
private:
    vk::Device device_;
    vk::UniqueSemaphore semaphore_;
    constexpr static auto semaphoreTypeInfo_ = vk::SemaphoreTypeCreateInfo(vk::SemaphoreType::eTimeline);

    [[nodiscard]] vk::Result wait(uint64_t value, uint64_t timeout) const { return device_.waitSemaphores(vk::SemaphoreWaitInfo{ {}, *semaphore_, value }, timeout); }
public:
    VulkanTimelineSemaphore(const vk::Device& device) :
        device_(device), semaphore_(device.createSemaphoreUnique({ {}, &semaphoreTypeInfo_ }))
    {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanTimelineSemaphore);

    const vk::Semaphore& get() const noexcept { return semaphore_.get(); }
    [[nodiscard]] vk::Result wait(uint64_t value, std::chrono::nanoseconds timeout) const { return wait(value, gsl::narrow_cast<uint64_t>(std::max(0ll, timeout.count()))); }
    [[nodiscard]] vk::Result wait(uint64_t value) const { return wait(value, std::chrono::nanoseconds::max()); }
    uint64_t counter() const { return device_.getSemaphoreCounterValue(*semaphore_); }
    void signal(uint64_t value) const { device_.signalSemaphore({ *semaphore_, value }); }
};

class VulkanFence
{
private:
    vk::Device device_;
    vk::UniqueFence fence_;

    [[nodiscard]] vk::Result wait(uint64_t timeout) const { return device_.waitForFences({ *fence_ }, true, timeout); }
public:
    VulkanFence(const vk::Device& device)
        : device_(device), fence_(device.createFenceUnique({}))
    {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanFence);

    const vk::Fence& get() const noexcept { return fence_.get(); }
    [[nodiscard]] vk::Result wait(std::chrono::nanoseconds timeout) const { return wait(gsl::narrow_cast<uint64_t>(std::max(0ll, timeout.count()))); }
    [[nodiscard]] vk::Result wait() const { return wait(std::chrono::nanoseconds::max()); }
    void reset() const { device_.resetFences({ *fence_ }); }
    /* Possible values are eSuccess, eNotReady, and eErrorDeviceLost */
    [[nodiscard]] vk::Result status() const { return device_.getFenceStatus(*fence_); }
};
