#pragma once

#include "vk_types.h"
#include "vk_validation.h"

#include <iostream>

struct VulkanDevice
{
    vk::raii::PhysicalDevice physicalDevice;
    vk::raii::Device device;
    vk::raii::Queue generalQueue;
    vk::raii::Queue transferQueue;

    VulkanDevice(
        vk::raii::PhysicalDevice& physicalDevice_,
        const vk::DeviceCreateInfo& deviceInfo,
        uint32_t generalQueueIndex,
        uint32_t transferQueueIndex
    ) :
        physicalDevice(physicalDevice_), device(physicalDevice.createDevice(deviceInfo)),
        generalQueue(device.getQueue(generalQueueIndex, 0u)),
        transferQueue(device.getQueue(transferQueueIndex, 0u))
    {}
};

class VulkanInstance
{
    vk::raii::Instance instance_;
    std::vector<std::shared_ptr<VulkanDevice>> devices_;

    vk::raii::Instance makeInstance(
        const vk::ApplicationInfo& appInfo,
        gsl::span<const char* const> validationLayers,
        gsl::span<const char* const> instanceExtensions)
    {
        checkValidationLayers(validationLayers);

        vk::raii::Context context;
        auto extensions = context.enumerateInstanceExtensionProperties();
        std::cout << "Available instance extensions:" << std::endl;
        for (auto& extension : extensions)
            std::cout << "\t" << extension.extensionName << std::endl;
        if constexpr (vk::enableValidationLayers)
        {
            std::cout << "Enabled validation layers:" << std::endl;
            for (auto layer : validationLayers)
                std::cout << "\t" << layer << std::endl;
        }
        else
            std::cout << "Validation layers disabled" << std::endl;

        std::cout << "Enabled instance extensions:" << std::endl;
        for (auto extension : instanceExtensions)
            std::cout << "\t" << extension << std::endl;

        return context.createInstance(vk::InstanceCreateInfo{ {}, &appInfo, validationLayers, instanceExtensions });
    }
public:
    VulkanInstance(
        const vk::ApplicationInfo& appInfo,
        gsl::span<const char* const> validationLayers,
        gsl::span<const char* const> instanceExtensions,
        gsl::span<const char* const> deviceExtensions
    ) :
        instance_(makeInstance(appInfo, validationLayers, instanceExtensions))
    {
        auto physicalDevices = instance_.enumeratePhysicalDevices();
        for (auto& physicalDevice : physicalDevices)
        {
            if (!physicalDevice.getFeatures().geometryShader)
                continue;

            auto extensions = physicalDevice.enumerateDeviceExtensionProperties();
            std::cout << "Available device extensions:" << std::endl;
            for (auto& extension : extensions)
                std::cout << "\t" << extension.extensionName << std::endl;

            uint32_t generalQueueIndex = std::numeric_limits<uint32_t>::max();
            uint32_t transferQueueIndex = std::numeric_limits<uint32_t>::max();
            const auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
            for (uint32_t queueIndex = 0u; auto & queueFamily : queueFamilyProperties)
            {
                constexpr vk::QueueFlags generalQueueFlags =
                    vk::QueueFlagBits::eGraphics & vk::QueueFlagBits::eCompute & vk::QueueFlagBits::eTransfer;
                if ((queueFamily.queueFlags & generalQueueFlags) == generalQueueFlags)
                    generalQueueIndex = queueIndex;
                else if (queueFamily.queueFlags == vk::QueueFlagBits::eTransfer)
                    transferQueueIndex = queueIndex;
                queueIndex++;
            }
            assert(generalQueueIndex < queueFamilyProperties.size());
            assert(transferQueueIndex < queueFamilyProperties.size());
            const std::array topPriority = { 1.0f };
            const std::array queueInfos = {
                vk::DeviceQueueCreateInfo({}, generalQueueIndex,  topPriority),
                vk::DeviceQueueCreateInfo({}, transferQueueIndex, topPriority),
            };

            vk::PhysicalDeviceVulkan12Features features12;
            features12.timelineSemaphore = true;
            vk::PhysicalDeviceVulkan13Features features13;
            features13.synchronization2 = true;
            features13.pNext = &features12;
            const vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, deviceExtensions, {}, &features13);

            devices_.push_back(std::make_shared<VulkanDevice>(
                physicalDevice, deviceInfo, generalQueueIndex, transferQueueIndex));
        }
    }

    const vk::raii::Instance& getInstance() const noexcept { return instance_; }
    gsl::span<const std::shared_ptr<VulkanDevice>> getDevices() const noexcept { return devices_; }
};
