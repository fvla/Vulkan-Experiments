#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include <array>
#include <concepts>
#include <numeric>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>
#include <gsl/gsl>

#define VK_CHECK(x)                                                     \
    do                                                                  \
    {                                                                   \
        const vk::Result err = x;                                       \
        using enum vk::Result;                                          \
        if (err != eSuccess && err != eTimeout)                         \
        {                                                               \
            std::stringstream ss;                                       \
            ss << "Detected Vulkan error on line " << __LINE__;         \
            ss << " of file " __FILE__ ": " << err;                     \
            throw FatalError(ss.str());                                 \
        }                                                               \
    } while (0)

#if defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_PLATFORM "win32"
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_PLATFORM "android"
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#define VK_PLATFORM "xcb"
#endif
#ifndef VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif

namespace rv = std::ranges::views;

namespace vk
{
#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif
}

template <auto T>
constexpr bool require_constexpr = true;
consteval auto as_constexpr(auto x) { return x; }

template <typename T>
concept Void = std::is_void_v<T>;

template <typename T>
concept Array = std::same_as<T, std::array<typename T::value_type, std::tuple_size<T>::value>>;
template <typename T, typename ValueType>
concept ArrayOf = Array<T> && std::same_as<typename T::value_type, ValueType>;
static_assert(ArrayOf<std::array<int, 0>, int>);
static_assert(ArrayOf<std::array<std::string, 1>, std::string>);

/* Credit to https://stackoverflow.com/a/66140851 */
template <typename T>
concept IsSpan = std::same_as<T, gsl::span<typename T::element_type, T::extent>>;
template <typename T>
concept Spannable = !IsSpan<T> && requires(T a) { { gsl::span(a) }; };
static_assert(Spannable<std::array<int, 5>>);

template <typename T, typename... Args>
constexpr bool is_same_all = (std::is_same_v<T, Args> && ...);

template <typename T, size_t... Sizes>
constexpr auto array_concat(const std::array<T, Sizes>&... arrays)
{
    constexpr auto total_length = (Sizes + ...);
    std::array<T, total_length> out{};
    auto outputIterator = out.begin();
    /* Fold expression to iterate over parameter pack with varying types */
    (
        [&]() {
            std::ranges::copy(arrays, outputIterator);
            outputIterator += arrays.size();
        }(), ...);
    return out;
}

template <typename T>
class OptionalReference
{
    T* pointer_ = nullptr;
public:
    constexpr OptionalReference() noexcept {}
    constexpr OptionalReference(T& reference) : pointer_(&reference) {}
    constexpr OptionalReference& operator=(T& reference) { pointer_ = &reference; return *this; }
    constexpr OptionalReference(const OptionalReference&) = default;
    constexpr OptionalReference(OptionalReference&&) = default;
    constexpr OptionalReference& operator=(const OptionalReference&) = default;
    constexpr OptionalReference& operator=(OptionalReference&&) = default;

    constexpr operator bool() const { return static_cast<bool>(pointer_); }
    constexpr bool has_value() const { return static_cast<bool>(pointer_); }

    template <typename F>
    constexpr void apply(F&& f) { if (pointer_) std::forward<F>(f)(*pointer_); }
    template <typename F>
    constexpr void apply(F&& f) const { if (pointer_) std::forward<F>(f)(*pointer_); }

    constexpr auto operator<=>(OptionalReference<T> other) { return pointer_ <=> other.pointer_; }
};

template <typename T>
concept Feature = requires
{
    typename T::Dependencies;

    as_constexpr(T::instanceExtensions);
    ArrayOf<decltype(T::instanceExtensions), const char*>;
    as_constexpr(T::deviceExtensions);
    ArrayOf<decltype(T::deviceExtensions), const char*>;
    as_constexpr(T::validationLayers);
    ArrayOf<decltype(T::validationLayers), const char*>;
};

template <Feature... Args>
struct FeatureList
{
private:
    template <typename T>
    struct hasFeature_ : std::bool_constant<(std::is_same_v<T, Args> || ...)> {};
    template <Feature... Args2>
    struct hasFeature_<FeatureList<Args2...>> : std::bool_constant<(hasFeature_<Args2>::value && ...)> {};
public:
    template <typename T>
    constexpr static bool hasFeature = hasFeature_<T>::value;

    using Dependencies = FeatureList<typename Args::Dependencies...>;

    constexpr static std::array instanceExtensions = array_concat(Args::instanceExtensions...);
    constexpr static std::array deviceExtensions = array_concat(Args::deviceExtensions...);
    constexpr static std::array validationLayers = array_concat(Args::validationLayers...);
};

struct EmptyFeature
{
    using Dependencies = FeatureList<>;
    constexpr static std::array<const char*, 0> instanceExtensions = {};
    constexpr static std::array<const char*, 0> deviceExtensions = {};
    constexpr static std::array<const char*, 0> validationLayers = {};
};

template <>
struct FeatureList<> : EmptyFeature {};

struct SwapchainFeature : EmptyFeature
{
    constexpr static std::array deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
};
struct PhysicalDevicePropertiesFeature : EmptyFeature
{
    constexpr static std::array instanceExtensions = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
};
struct SemaphoreFeature : EmptyFeature
{
    using Dependencies = FeatureList<PhysicalDevicePropertiesFeature>;
    constexpr static std::array instanceExtensions = {
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
    };
    constexpr static std::array deviceExtensions = {
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME "_" VK_PLATFORM,
    };
};
struct TimelineSemaphoreFeature : EmptyFeature
{
    using Dependencies = FeatureList<SemaphoreFeature>;
    constexpr static std::array deviceExtensions = {
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    };
};
struct FenceFeature : EmptyFeature
{
    using Dependencies = FeatureList<PhysicalDevicePropertiesFeature>;
    constexpr static std::array instanceExtensions = {
        VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
    };
    constexpr static std::array deviceExtensions = {
        VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME "_" VK_PLATFORM,
    };
};
struct SurfaceFeature : EmptyFeature
{
    constexpr static std::array instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };
};
struct SDLFeature : EmptyFeature
{
    using Dependencies = FeatureList<SurfaceFeature>;
};
struct HeadlessSurfaceFeature : EmptyFeature // Doesn't seem portable
{
    using Dependencies = FeatureList<SurfaceFeature>;
    constexpr static std::array instanceExtensions = {
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
};
struct GLFWFeature : EmptyFeature {};
struct ValidationLayerFeature : EmptyFeature
{
    constexpr static std::array instanceExtensions = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    constexpr static std::array validationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };
};
using ValidationLayerFeatureIfEnabled = std::conditional_t<
    vk::enableValidationLayers, ValidationLayerFeature, EmptyFeature
>;

using AvailableFeatures = FeatureList<
    SurfaceFeature,
    SDLFeature,
    PhysicalDevicePropertiesFeature,
    FenceFeature,
    SemaphoreFeature,
    SwapchainFeature,
    ValidationLayerFeatureIfEnabled
>;
static_assert(AvailableFeatures::hasFeature<AvailableFeatures::Dependencies>, "Dependencies not all satisfied in AvailableFeatures");

template <Feature T, Void V = void>
struct RequiresFeature
{
    static_assert((Void<V>) && AvailableFeatures::hasFeature<T>);
};

template <Void V = void>
class GLFWOutput : RequiresFeature<GLFWFeature, V>
{

};

struct FatalError : std::runtime_error
{
    using std::runtime_error::runtime_error;
    FatalError() = delete;
};

struct VulkanQueueInfo
{
    uint32_t familyIndex;
    uint32_t index;
    vk::Queue queue;

    VulkanQueueInfo(uint32_t familyIndex_, uint32_t index_, vk::Queue queue_) noexcept
        : familyIndex(familyIndex_), index(index_), queue(queue_) {}
};

struct VertexInfo
{
private:
    std::vector<vk::VertexInputBindingDescription> bindings_;
    std::vector<vk::VertexInputAttributeDescription> attributes_;
public:
    vk::PipelineVertexInputStateCreateInfo info;

    VertexInfo(std::vector<vk::VertexInputBindingDescription>&& bindings,
               std::vector<vk::VertexInputAttributeDescription>&& attributes)
        : bindings_(std::move(bindings)), attributes_(std::move(attributes)), info({}, bindings_, attributes_)
    {}
};

struct SimpleVertex
{
    glm::vec3 position;
    glm::vec3 color;

    static VertexInfo getVertexInputInfo()
    {
        return VertexInfo({ { 0, sizeof(SimpleVertex) } },
                          { { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(SimpleVertex, position) },
                            { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(SimpleVertex, color) } });
    }
};
