#pragma once

#include "vk_types.h"
#include "vk_command.h"

#include <memory>
#include <optional>

struct SDL_Window;

template <Void V = void>
class VulkanSwapchain : RequiresFeature<SwapchainFeature, V>
{
    vk::Device device_;
    vk::UniqueSwapchainKHR swapchain_;
    std::vector<vk::Image> swapchainImages_;
    std::vector<vk::UniqueImageView> swapchainImageViews_;
    std::vector<vk::UniqueFramebuffer> swapchainFramebuffers_;
public:
    VulkanSwapchain(const vk::PhysicalDevice& physicalDevice, const vk::Device& device, const vk::RenderPass& renderPass,
                    const vk::SurfaceKHR& surface, const vk::SurfaceFormatKHR& surfaceFormat, const vk::Extent2D& imageExtent)
        : device_(device)
    {
        auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
        auto presentMode = vk::PresentModeKHR::eFifo;
        for (auto& mode : presentModes)
            if (mode == vk::PresentModeKHR::eMailbox)
            {
                presentMode = mode;
                break;
            }
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
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
        swapchain_ = device_.createSwapchainKHRUnique(swapchainInfo);
        swapchainImages_ = device_.getSwapchainImagesKHR(*swapchain_);
        for (auto& image : swapchainImages_)
        {
            const auto& imageViewInfo = vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, surfaceFormat.format)
                .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
            swapchainImageViews_.push_back(device_.createImageViewUnique(imageViewInfo));
        }
        for (auto& imageView : swapchainImageViews_)
        {
            std::array attachments = { *imageView };
            const vk::FramebufferCreateInfo framebufferInfo({}, renderPass, attachments, imageExtent.width, imageExtent.height, 1u);
            swapchainFramebuffers_.push_back(device_.createFramebufferUnique(framebufferInfo));
        }
    }
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    uint32_t acquireNextImage(const vk::Semaphore& semaphore) const
    {
        const auto resultValue = device_.acquireNextImageKHR(*swapchain_, std::numeric_limits<uint64_t>::max(), semaphore);
        if (resultValue.result != vk::Result::eSuccess)
            throw FatalError("Swapchain failed to acquire next image");
        return resultValue.value;
    }
    const vk::SwapchainKHR& getSwapchain() const noexcept { return *swapchain_; }
    const size_t getFramebufferCount() const noexcept { return swapchainFramebuffers_.size(); }
    const size_t size() const noexcept { return swapchainFramebuffers_.size(); }
    const vk::Framebuffer& getFramebuffer(size_t index) const { return *swapchainFramebuffers_.at(index); }
};
