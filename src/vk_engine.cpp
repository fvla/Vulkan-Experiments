#include "vk_engine.h"
#include "vk_types.h"

#include <vulkan/vulkan_raii.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>

using std::ranges::iota_view;
using std::views::zip;

using AvailableFeatures = ValidatedFeatureList<
    SurfaceFeature,
    SDLFeature,
    PhysicalDevicePropertiesFeature,
    FenceFeature,
    SemaphoreFeature,
    TimelineSemaphoreFeature,
    SwapchainFeature,
    ValidationLayerFeatureIfEnabled
>;

static SDL_Window* createWindow(vk::Extent2D windowExtent)
{
    SDL_Init(SDL_INIT_VIDEO);

    constexpr auto window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

    SDL_Window* window = SDL_CreateWindow(
        "Vulkan Engine", //window title
        SDL_WINDOWPOS_UNDEFINED, //window position x (don't care)
        SDL_WINDOWPOS_UNDEFINED, //window position y (don't care)
        gsl::narrow<int>(windowExtent.width),  //window width in pixels
        gsl::narrow<int>(windowExtent.height), //window height in pixels
        window_flags
    );
    if (!window)
        throw FatalError(SDL_GetError());
    return window;
}

static vk::raii::SurfaceKHR getSurface(SDL_Window* window, const VulkanInstance& instance)
{
    VkSurfaceKHR surfaceRaw;
    if (!SDL_Vulkan_CreateSurface(window, *instance.getInstance(), &surfaceRaw))
        throw FatalError(std::string("Failed to create SDL Vulkan surface: ") + SDL_GetError());
    return vk::raii::SurfaceKHR(instance.getInstance(), surfaceRaw);
}

using DevicePointer = std::shared_ptr<const VulkanDevice>;

static auto selectDevice(const VulkanInstance& instance)
{
    std::vector<DevicePointer> devices;
    {
        const auto devicesView = instance.getDevices();
        devices.reserve(devicesView.size());
        std::ranges::copy(devicesView, std::back_inserter(devices));
    }
    constexpr int BAD_DEVICE_SCORE = -1;
    /* Assign device suitability score. Greater is better. */
    const auto deviceScorer = [](const DevicePointer& device) noexcept -> int
    {
        const auto deviceProperties = device->physicalDevice.getProperties();
        const auto deviceFeatures = device->physicalDevice.getFeatures();

        if (!deviceFeatures.geometryShader)
            return BAD_DEVICE_SCORE;
        if (!device->generalQueue)
            return BAD_DEVICE_SCORE;

        int score = 0;
        if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            score += 1000;
        if (device->transferQueue)
            score += 300;

        return score;
    };
    // Sort devices descending by score
    std::ranges::sort(devices, std::ranges::greater{}, deviceScorer);
    const auto& device = devices.at(0);
    if (deviceScorer(device) == BAD_DEVICE_SCORE)
        throw FatalError("Failed to find a suitable GPU for Vulkan rendering");
    return gsl::not_null(device);
}

static vk::SurfaceFormatKHR selectSurfaceFormat(const vk::SurfaceKHR& surface, const VulkanDevice& device)
{
    auto surfaceFormats = device.physicalDevice.getSurfaceFormatsKHR(surface);
    for (auto& format : surfaceFormats)
        if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            return format;
    throw FatalError("Could not find a suitable surface");
}

static vk::raii::RenderPass createRenderPass(const vk::SurfaceFormatKHR& surfaceFormat, const VulkanDevice& device)
{
    auto colorAttachment = vk::AttachmentDescription({}, surfaceFormat.format, vk::SampleCountFlagBits::e1,
                                                     vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                     vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                                     vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
    std::array attachments = { colorAttachment };
    std::array colorAttachmentRefs = { vk::AttachmentReference(0u, vk::ImageLayout::eColorAttachmentOptimal) };
    std::array subpasses = { vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRefs) };
    std::array subpassDependencies = {
        vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0u, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                              vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite)
    };
    const vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpasses, subpassDependencies);
    return device.device.createRenderPass(renderPassInfo);
}

static vk::raii::ShaderModule createShader(const std::string& filename, const VulkanDevice& device)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    const size_t shaderSize = static_cast<size_t>(file.tellg());
    std::vector<char> shaderBytes(shaderSize);
    file.seekg(0);
    file.read(shaderBytes.data(), shaderBytes.size());
    std::vector<uint32_t> shaderContents((shaderSize + sizeof(uint32_t) - 1) / sizeof(uint32_t)); // ceil of shaderSize / 4
    std::ranges::copy(
        gsl::as_writable_bytes(gsl::span(shaderBytes)),
        gsl::as_writable_bytes(gsl::span(shaderContents)).begin()
    );

    const vk::ShaderModuleCreateInfo shaderInfo({}, shaderContents);
    return device.device.createShaderModule(shaderInfo);
};

static vk::raii::PipelineLayout createPipelineLayout(const VulkanDevice& device)
{
    const auto vertexPushConstant = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstants));
    const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo({}, {}, vertexPushConstant);
    return device.device.createPipelineLayout(pipelineLayoutInfo);
}

static vk::raii::Pipeline createPipeline(const vk::RenderPass& renderPass,
                                         const vk::PipelineLayout& pipelineLayout,
                                         const vk::Extent2D& windowExtent,
                                         const VulkanDevice& device)
{
    const auto vertexShaderModule = createShader("shaders/vertex_shader.spv", device);
    const auto fragmentShaderModule = createShader("shaders/fragment_shader.spv", device);
    //std::array dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    const auto viewport = vk::Viewport(0.0f, 0.0f, static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height), 0.0f, 1.0f);
    const auto scissor = vk::Rect2D({ 0, 0 }, windowExtent);
    using enum vk::ColorComponentFlagBits;
    std::array colorBlendAttachments = { vk::PipelineColorBlendAttachmentState(false).setColorWriteMask(eR | eG | eB | eA)};

    auto vertexShaderStageInfo      = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexShaderModule, "main");
    auto fragmentShaderStageInfo    = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentShaderModule, "main");
    const std::array shaderStages   = { vertexShaderStageInfo, fragmentShaderStageInfo };
    const auto dynamicStateInfo     = vk::PipelineDynamicStateCreateInfo({}, /*dynamicStates*/ {});
    const VertexInfo vertexInfo     = SimpleVertex::getVertexInputInfo();
    const auto& vertexInputInfo     = vertexInfo.info;
    const auto inputAssemblyInfo    = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList);
    const auto viewportInfo         = vk::PipelineViewportStateCreateInfo({}, viewport, scissor);
    const auto rasterizationInfo    = vk::PipelineRasterizationStateCreateInfo({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
                                                                                vk::FrontFace::eClockwise, false, {}, {}, {}, 1.0f);
    const auto multisampleInfo      = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false, 1.0f, nullptr, false, false);
    const auto colorBlendInfo       = vk::PipelineColorBlendStateCreateInfo({}, false, {}, colorBlendAttachments);

    const auto graphicsPipelineInfo = vk::GraphicsPipelineCreateInfo({}, shaderStages, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo,
                                                                     &rasterizationInfo, &multisampleInfo, nullptr, &colorBlendInfo, &dynamicStateInfo,
                                                                     pipelineLayout, renderPass, 0);
    auto pipeline = device.device.createGraphicsPipeline(nullptr, graphicsPipelineInfo);
    return pipeline;
}

void runEngine()
{
    static vk::Extent2D windowExtent{ 1280 , 720 };
    static const std::unique_ptr window =
        std::unique_ptr<SDL_Window, void (*)(SDL_Window*)>(createWindow(windowExtent), &SDL_DestroyWindow);
    static const auto instance = VulkanInstance(
        vk::ApplicationInfo("Triangle", VK_MAKE_API_VERSION(0, 1, 0, 0), "No Engine", 0, VK_API_VERSION_1_3),
        gsl::make_span(AvailableFeatures::validationLayers),
        gsl::make_span(AvailableFeatures::instanceExtensions),
        gsl::make_span(AvailableFeatures::deviceExtensions)
    );
    static const auto device = selectDevice(instance);
    if (!device->generalQueue)
        throw FatalError("Failed to acquire general queue from device");

    const auto surface = getSurface(window.get(), instance);
    const auto surfaceFormat = selectSurfaceFormat(*surface, *device);
    const auto swapchain = VulkanSwapchain(*device, *surface, surfaceFormat, windowExtent);

    const auto renderPass = createRenderPass(surfaceFormat, *device);
    const auto pipelineLayout = createPipelineLayout(*device);
    const auto pipeline = createPipeline(*renderPass, *pipelineLayout, windowExtent, *device);

    const auto commandPool = std::make_shared<VulkanCommandPool>(*device->device, 16u, *device->generalQueue);
    VulkanGraphicsStream stream(*device->device, commandPool);

    for (;;)
    {
        for (SDL_Event e{ 0 }; SDL_PollEvent(&e) != 0; )
        {
            switch (e.type)
            {
            case SDL_QUIT:
                throw QuitException();
            case SDL_WINDOWEVENT:
                switch (e.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    windowExtent.width = gsl::narrow<uint32_t>(e.window.data1);
                    windowExtent.height = gsl::narrow<uint32_t>(e.window.data2);
                    return;
                }
                break;
            }
        }

        const uint32_t imageIndex = stream.acquireNextImage(*device->generalQueue->queue, swapchain);
        const auto framebuffer = device->device.createFramebuffer(
            vk::FramebufferCreateInfo({}, *renderPass, *swapchain.getImageView(imageIndex),
                                      windowExtent.width, windowExtent.height, 1u));
        static constexpr auto clearValues = std::to_array<vk::ClearValue>({
            vk::ClearColorValue({std::array{0.0f, 0.0f, 0.0f, 1.0f}}),
        });
        const auto renderPassInfo =
            vk::RenderPassBeginInfo(*renderPass, *framebuffer, vk::Rect2D({}, windowExtent), clearValues);
        auto recorder = [](vk::CommandBuffer) {};
        stream.submitWork(*device->generalQueue->queue, renderPassInfo, recorder);
        stream.present(*device->generalQueue->queue, swapchain, imageIndex);
        stream.synchronize();
    }
}
