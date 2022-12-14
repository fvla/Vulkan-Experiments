#include "vk_engine.h"
#include "vk_validation.h"
#include "vk_types.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <fstream>
#include <iostream>

VulkanEngine::VulkanEngine()
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    window_ = SDL_CreateWindow(
        "Vulkan Engine", //window title
        SDL_WINDOWPOS_UNDEFINED, //window position x (don't care)
        SDL_WINDOWPOS_UNDEFINED, //window position y (don't care)
        static_cast<int>(windowExtent_.width),  //window width in pixels
        static_cast<int>(windowExtent_.height), //window height in pixels
        window_flags
    );
    if (!window_)
        throw FatalError(SDL_GetError());

    {
        auto extensions = vk::enumerateInstanceExtensionProperties();
        std::cout << "Available instance extensions:" << std::endl;
        for (auto& extension : extensions)
            std::cout << "\t" << extension.extensionName << std::endl;
    }
    // Instance
    {
        vk::ApplicationInfo appInfo("Triangle", VK_MAKE_API_VERSION(0, 1, 0, 0), "No Engine", 0, VK_API_VERSION_1_0);
        checkValidationLayers();
        if constexpr (vk::enableValidationLayers)
        {
            std::cout << "Enabled validation layers:" << std::endl;
            for (auto layer : AvailableFeatures::validationLayers)
                std::cout << "\t" << layer << std::endl;
        }
        else
            std::cout << "Validation layers disabled" << std::endl;

        std::cout << "Enabled instance extensions:" << std::endl;
        for (auto extension : AvailableFeatures::instanceExtensions)
            std::cout << "\t" << extension << std::endl;
        vk::InstanceCreateInfo createInfo({}, &appInfo, AvailableFeatures::validationLayers, AvailableFeatures::instanceExtensions);
        instance_ = vk::createInstanceUnique(createInfo);
    }
    {
        auto extensions = vk::enumerateInstanceExtensionProperties();
        std::cout << "Available instance extensions:" << std::endl;
        for (auto& extension : extensions)
            std::cout << "\t" << extension.extensionName << std::endl;
    }
    // Surface
    {
        vk::SurfaceKHR surface;
        if (!SDL_Vulkan_CreateSurface(window_, *instance_, &reinterpret_cast<VkSurfaceKHR&>(surface)))
            throw FatalError(std::string("Failed to create SDL Vulkan surface: ") + SDL_GetError());
        surface_ = vk::UniqueSurfaceKHR(surface, *instance_);
    }
    // Physical device
    {
        auto isDeviceSuitable = [this](const vk::PhysicalDevice& device)
        {
            auto deviceProperties = device.getProperties();
            auto deviceFeatures = device.getFeatures();
            if (!(deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader))
                return false;
            auto queueFamilyProperties = device.getQueueFamilyProperties();
            for (uint32_t queueIndex = 0; auto & queueFamily : queueFamilyProperties)
            {
                if ((queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) && device.getSurfaceSupportKHR(queueIndex, *surface_))
                    return true;
                queueIndex++;
            }
            return false;
        };
        auto physicalDevices = instance_->enumeratePhysicalDevices();
        auto physicalDeviceIt = std::find_if(physicalDevices.begin(), physicalDevices.end(), isDeviceSuitable);
        if (physicalDeviceIt == physicalDevices.end())
            throw FatalError("Failed to find a suitable GPU for Vulkan rendering");
        physicalDevice_ = *physicalDeviceIt;
    }
    {
        auto extensions = physicalDevice_.enumerateDeviceExtensionProperties();
        std::cout << "Available device extensions:" << std::endl;
        for (auto& extension : extensions)
            std::cout << "\t" << extension.extensionName << std::endl;
    }
    // Surface format
    {
        auto surfaceFormats = physicalDevice_.getSurfaceFormatsKHR(*surface_);
        surfaceFormat_ = vk::SurfaceFormatKHR();
        for (auto& format : surfaceFormats)
            if (format.format == vk::Format::eB8G8R8A8Srgb &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                surfaceFormat_ = format;
                break;
            }
        if (surfaceFormat_ == vk::SurfaceFormatKHR())
            throw FatalError("Could not find a suitable surface");
    }
    // Logical device
    {
        uint32_t queueFamilyIndex = 0U; // Will always be set by the following loop
        auto queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();
        for (uint32_t queueIndex = 0; auto & queueFamily : queueFamilyProperties)
        {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics && physicalDevice_.getSurfaceSupportKHR(queueIndex, *surface_))
            {
                queueFamilyIndex = queueIndex;
                break;
            }
            queueIndex++;
        }
        std::array queuePriorities = { 1.0f };
        std::array queueInfos = {
            vk::DeviceQueueCreateInfo({}, queueFamilyIndex, queuePriorities),
        };

        std::cout << "Enabled device extensions:" << std::endl;
        for (auto extension : AvailableFeatures::deviceExtensions)
            std::cout << "\t" << extension << std::endl;

        vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, AvailableFeatures::deviceExtensions);
        device_ = physicalDevice_.createDeviceUnique(deviceInfo);
        for (auto& queueInfo : queueInfos)
            for (uint32_t queueIndex = 0; queueIndex < queueInfo.queueCount; queueIndex++)
                graphicsQueues_.emplace_back(queueInfo.queueFamilyIndex, queueIndex, device_->getQueue(queueInfo.queueFamilyIndex, queueIndex));
    }
    // Render pass
    {
        auto colorAttachment = vk::AttachmentDescription({}, surfaceFormat_.format, vk::SampleCountFlagBits::e1,
                                                         vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                         vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                                         vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
        std::array attachments = { colorAttachment };
        std::array colorAttachmentRefs = { vk::AttachmentReference(0u, vk::ImageLayout::eAttachmentOptimal) };
        std::array subpasses = { vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRefs) };
        std::array subpassDependencies = {
            vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0u, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite)
        };
        vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpasses, subpassDependencies);
        renderPass_ = device_->createRenderPassUnique(renderPassInfo);
    }
    // Shaders and full render pipeline
    {
        auto createShader = [this](const std::string& filename)
        {
            std::ifstream file(filename, std::ios::ate | std::ios::binary);
            const size_t shaderSize = static_cast<size_t>(file.tellg());
            std::vector<uint32_t> shaderContents((shaderSize + sizeof(uint32_t) - 1) / sizeof(uint32_t)); // ceil of shaderSize / 4
            file.seekg(0);
            file.read(reinterpret_cast<char*>(shaderContents.data()), static_cast<std::streamsize>(shaderSize));

            vk::ShaderModuleCreateInfo shaderInfo({}, shaderContents);
            return device_->createShaderModuleUnique(shaderInfo);
        };
        auto vertexShaderModule = createShader("shaders/vertex_shader.spv");
        auto fragmentShaderModule = createShader("shaders/fragment_shader.spv");
        std::array dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        viewports_ = { vk::Viewport(0.0f, 0.0f, static_cast<float>(windowExtent_.width), static_cast<float>(windowExtent_.height), 0.0f, 1.0f) };
        scissors_ = { vk::Rect2D({ 0, 0 }, windowExtent_) };
        using enum vk::ColorComponentFlagBits;
        std::array colorBlendAttachments = { vk::PipelineColorBlendAttachmentState(false).setColorWriteMask(eR | eG | eB | eA)};

        auto vertexShaderStageInfo      = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexShaderModule, "main");
        auto fragmentShaderStageInfo    = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentShaderModule, "main");
        std::array shaderStages         = { vertexShaderStageInfo, fragmentShaderStageInfo };
        auto dynamicStateInfo           = vk::PipelineDynamicStateCreateInfo({}, dynamicStates);
        VertexInfo vertexInfo           = SimpleVertex::getVertexInputInfo();
        auto& vertexInputInfo           = vertexInfo.info;
        auto inputAssemblyInfo          = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList);
        auto viewportInfo               = vk::PipelineViewportStateCreateInfo({}, viewports_, scissors_);
        auto rasterizationInfo          = vk::PipelineRasterizationStateCreateInfo({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
                                                                                   vk::FrontFace::eClockwise, false, {}, {}, {}, 1.0f);
        auto multisampleInfo            = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false, 1.0f, nullptr, false, false);
        auto colorBlendInfo             = vk::PipelineColorBlendStateCreateInfo({}, false, {}, colorBlendAttachments);
        auto pipelineLayoutInfo         = vk::PipelineLayoutCreateInfo();
        pipelineLayout_ = device_->createPipelineLayoutUnique(pipelineLayoutInfo);

        auto graphicsPipelineInfo       = vk::GraphicsPipelineCreateInfo({}, shaderStages, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo,
                                                                         &rasterizationInfo, &multisampleInfo, nullptr, &colorBlendInfo, &dynamicStateInfo,
                                                                         *pipelineLayout_, *renderPass_, 0);
        vk::Result pipelineResult;
        std::tie(pipelineResult, pipeline_) = device_->createGraphicsPipelineUnique({}, graphicsPipelineInfo).asTuple();
        if (pipelineResult != vk::Result::eSuccess)
            throw FatalError("Graphics pipeline creation failed.");
    }
    // Swapchain, image views, and framebuffers
    {
        swapchain_.emplace(physicalDevice_, *device_, *renderPass_, *surface_, surfaceFormat_, windowExtent_);
    }
    // Vertex and index buffers
    {
        /*vk::BufferCreateInfo vertexBufferInfo({}, sizeof(vertexBufferArray_), vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, {});
        vertexBuffer_ = device_->createBufferUnique(vertexBufferInfo);*/
    }
    // Command pool and triangle pipeline
    {
        commandHandlerPool_ = VulkanCommandHandlerPool(*device_, swapchain_->size(), graphicsQueues_.back());
        for (size_t i = 0; auto& commandHandler : commandHandlerPool_)
        {
            trianglePipelines_.emplace_back(physicalDevice_, *device_, commandHandlerPool_, *pipeline_, graphicsQueues_.back(), viewports_, scissors_);
            vk::RenderPassBeginInfo renderPassInfo(*renderPass_, swapchain_->getFramebuffer(i), vk::Rect2D({}, windowExtent_), clearValues_);
            commandHandler.record(renderPassInfo, [this](const vk::CommandBuffer& commandBuffer) { trianglePipelines_.back().recordTriangleCommand(commandBuffer); });
            i++;
        }
    }
}

VulkanEngine::~VulkanEngine()
{
    SDL_DestroyWindow(window_);
}

void VulkanEngine::recreateSwapchain()
{
    viewports_ = { vk::Viewport(0.0f, 0.0f, static_cast<float>(windowExtent_.width), static_cast<float>(windowExtent_.height), 0.0f, 1.0f) };
    scissors_ = { vk::Rect2D({ 0, 0 }, windowExtent_) };
    swapchain_.emplace(physicalDevice_, *device_, *renderPass_, *surface_, surfaceFormat_, windowExtent_);
    for (size_t i = 0; auto & commandHandler : commandHandlerPool_)
    {
        vk::RenderPassBeginInfo renderPassInfo(*renderPass_, swapchain_->getFramebuffer(i), vk::Rect2D({}, windowExtent_), clearValues_);
        commandHandler.record(renderPassInfo, [this](const vk::CommandBuffer& commandBuffer) { trianglePipelines_.back().recordTriangleCommand(commandBuffer); });
        i++;
    }
}

void VulkanEngine::draw()
{
    trianglePipelines_[frameNumber_ % trianglePipelines_.size()].run(*swapchain_, graphicsQueues_.back().queue);
    frameNumber_++;
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    //main loop
    while (!bQuit)
    {
        //Handle events on queue
        while (SDL_PollEvent(&e) != 0)
        {
            //close the window when user clicks the X button or alt-f4s
            if (e.type == SDL_QUIT) bQuit = true;
        }

        draw();
    }
}
