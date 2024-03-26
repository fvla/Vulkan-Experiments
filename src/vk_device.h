#pragma once

#include "vk_types.h"

struct VulkanDevice
{
    vk::raii::PhysicalDevice physicalDevice;
    vk::raii::Device device;
    std::optional<vk::raii::Queue> generalQueue;
    std::optional<vk::raii::Queue> transferQueue;

    VulkanDevice(
        vk::raii::PhysicalDevice& physicalDevice_,
        const vk::DeviceCreateInfo& deviceInfo,
        std::optional<uint32_t> generalQueueIndex,
        std::optional<uint32_t> transferQueueIndex
    ) :
        physicalDevice(physicalDevice_), device(physicalDevice.createDevice(deviceInfo)),
        generalQueue(getQueue_(generalQueueIndex)),
        transferQueue(getQueue_(transferQueueIndex))
    {}
private:
    std::optional<vk::raii::Queue> getQueue_(std::optional<uint32_t> queueIndex) const
    {
        return queueIndex.transform([this](uint32_t i) { return device.getQueue(i, 0u); });
    }
};
