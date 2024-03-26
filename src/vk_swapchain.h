#pragma once

#include "vk_types.h"
#include "vk_command.h"
#include "vk_device.h"

#include <memory>
#include <optional>

struct SDL_Window;

class VulkanSwapchain
{
    vk::raii::SwapchainKHR swapchain_;
    std::vector<vk::Image> swapchainImages_;
    std::vector<vk::raii::ImageView> swapchainImageViews_;
public:
    VulkanSwapchain(const VulkanDevice& device,
                    const vk::SurfaceKHR& surface,
                    const vk::SurfaceFormatKHR& surfaceFormat,
                    const vk::Extent2D& imageExtent)
        : swapchain_(createSwapchain(device, surface, surfaceFormat, imageExtent))
    {
        swapchainImages_ = swapchain_.getImages();
        for (auto& image : swapchainImages_)
        {
            const auto& imageViewInfo = vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, surfaceFormat.format)
                .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
            swapchainImageViews_.push_back(device.device.createImageView(imageViewInfo));
        }
    }
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    uint32_t acquireNextImage(const vk::Semaphore& semaphore) const
    {
        const auto [result, imageValue] = swapchain_.acquireNextImage(std::numeric_limits<uint64_t>::max(), semaphore);
        if (result != vk::Result::eSuccess)
            throw FatalError("Swapchain failed to acquire next image");
        return imageValue;
    }
    const vk::raii::SwapchainKHR& getSwapchain() const noexcept { return swapchain_; }
    size_t size() const noexcept { return swapchainImageViews_.size(); }
    const vk::raii::ImageView& getImageView(size_t index) const { return swapchainImageViews_.at(index); }
private:
    static vk::raii::SwapchainKHR createSwapchain(const VulkanDevice& device,
                                                  const vk::SurfaceKHR& surface,
                                                  const vk::SurfaceFormatKHR& surfaceFormat,
                                                  const vk::Extent2D& imageExtent)
    {
        auto presentModes = device.physicalDevice.getSurfacePresentModesKHR(surface);
        auto presentMode = vk::PresentModeKHR::eFifo;
        for (auto& mode : presentModes)
            if (mode == vk::PresentModeKHR::eMailbox)
            {
                presentMode = mode;
                break;
            }
        const auto surfaceCapabilities = device.physicalDevice.getSurfaceCapabilitiesKHR(surface);
        uint32_t imageCount = 2;
        if (surfaceCapabilities.maxImageCount != 0)
            imageCount = std::min(imageCount, surfaceCapabilities.maxImageCount);
        const vk::SwapchainCreateInfoKHR swapchainInfo(
            {}, surface,
            imageCount, surfaceFormat.format, surfaceFormat.colorSpace,
            imageExtent, 1, vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive, {},
            surfaceCapabilities.currentTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque, presentMode,
            false);
        return device.device.createSwapchainKHR(swapchainInfo);
    }
};
