#pragma once

#include "vk_types.h"

struct VulkanDevice
{
    vk::raii::PhysicalDevice physicalDevice;
    vk::raii::Device device;
    std::optional<VulkanQueueInfo> generalQueue;
    std::optional<VulkanQueueInfo> transferQueue;

    VulkanDevice(
        vk::raii::PhysicalDevice physicalDevice_,
        const vk::DeviceCreateInfo& deviceInfo,
        std::optional<uint32_t> generalQueueIndex,
        std::optional<uint32_t> transferQueueIndex
    ) :
        physicalDevice(std::move(physicalDevice_)),
        device(physicalDevice.createDevice(deviceInfo)),
        generalQueue(getQueue_(generalQueueIndex)),
        transferQueue(getQueue_(transferQueueIndex))
    {}
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanDevice)
private:
    std::optional<VulkanQueueInfo>
    getQueue_(std::optional<uint32_t> queueFamilyIndex)
    {
        return queueFamilyIndex.transform([this](uint32_t i)
        { return VulkanQueueInfo(i, 0u, device.getQueue(i, 0u)); });
    }
};
