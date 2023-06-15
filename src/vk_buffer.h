#pragma once

#include "vk_types.h"
#include "vk_command.h"

#include <chrono>
#include <limits>
#include <numeric>

enum class VulkanBufferType
{
    DeviceLocal,
    Staging,
    HostAccessible
};

consteval vk::MemoryPropertyFlags getMemoryFlags(VulkanBufferType type)
{
    using enum VulkanBufferType;
    using enum vk::MemoryPropertyFlagBits;
    switch (type)
    {
    case DeviceLocal:       return eDeviceLocal;
    case Staging:           return eHostVisible | eHostCoherent;
    case HostAccessible:    return eDeviceLocal | eHostVisible;
    }
}

template <vk::BufferUsageFlags usage, VulkanBufferType bufferType>
class VulkanBuffer
{
private:
    vk::Device device_;
    vk::DeviceSize bufferSize_;
    vk::UniqueBuffer buffer_;
    vk::DeviceSize bufferCapacity_;
    vk::UniqueDeviceMemory bufferMemory_;

    constexpr static vk::MemoryPropertyFlags memoryPropertyFlags_ = getMemoryFlags(bufferType);
public:
    VulkanBuffer(const vk::PhysicalDevice& physicalDevice, const vk::Device& device, vk::DeviceSize bufferSize)
        : device_(device), bufferSize_(bufferSize)
    {
        buffer_ = device_.createBufferUnique(vk::BufferCreateInfo({}, bufferSize, usage, vk::SharingMode::eExclusive, {}));
        vk::MemoryRequirements memoryRequirements = device_.getBufferMemoryRequirements(*buffer_);
        const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
        bufferCapacity_ = memoryRequirements.size;

        constexpr auto findMemoryType = [](auto& memoryRequirements, const auto& memoryProperties)
        {
            for (int i = std::popcount(memoryRequirements.memoryTypeBits); --i >= 0; )
            {
                const uint32_t memoryType = gsl::narrow<uint32_t>(std::countr_zero(memoryRequirements.memoryTypeBits));
                if (memoryType > memoryProperties.memoryTypeCount)
                    break;
                if ((memoryProperties.memoryTypes.at(memoryType).propertyFlags & memoryPropertyFlags_) == memoryPropertyFlags_)
                    return memoryType;
                memoryRequirements.memoryTypeBits ^= 1 << memoryType;
            }
            throw FatalError("Failed to find suitable memory type for VulkanBuffer");
        };
        bufferMemory_ = device_.allocateMemoryUnique({ bufferCapacity_, findMemoryType(memoryRequirements, memoryProperties) });
        device_.bindBufferMemory(*buffer_, *bufferMemory_, 0);
    }
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = default;
    VulkanBuffer& operator=(VulkanBuffer&&) = default;

    const vk::Buffer& get() const noexcept { return *buffer_; }
    constexpr vk::DeviceSize size() const { return bufferSize_; }
    constexpr vk::DeviceSize capacity() const { return bufferCapacity_; }
    template <typename T, size_t N>
    void copyFrom(const gsl::span<const T, N> data) const
    {
        static_assert(bufferType == VulkanBufferType::Staging);
        assert(data.size_bytes() <= bufferSize_);
        void* bufferPointer = device_.mapMemory(*bufferMemory_, 0, data.size_bytes());
        std::memcpy(bufferPointer, data.data(), data.size_bytes());
        device_.unmapMemory(*bufferMemory_);
    }
    template <typename T, size_t N>
    void copyTo(const gsl::span<T, N> data) const
    {
        static_assert(bufferType == VulkanBufferType::Staging);
        assert(data.size_bytes() >= bufferSize_);
        const T* bufferPointer = reinterpret_cast<T*>(device_.mapMemory(*bufferMemory_, 0, bufferSize_));
        std::memcpy(data.data(), bufferPointer, data.size_bytes());
        device_.unmapMemory(*bufferMemory_);
    }
};

/* Vulkan buffer copies are done through command buffers, so this returns a VulkanCommandRecorder. */
template <vk::BufferUsageFlags usage, VulkanBufferType bufferType, vk::BufferUsageFlags usage2, VulkanBufferType bufferType2>
constexpr inline VulkanCommandRecorder auto recordCopyBuffers(const VulkanBuffer<usage, bufferType>& source,
                                                              const VulkanBuffer<usage2, bufferType2>& destination)
{
    using enum vk::BufferUsageFlagBits;
    static_assert(eTransferSrc | usage, "Source buffer must have TransferSrc buffer usage flag");
    static_assert(eTransferDst | usage2, "Destination buffer must have TransferDst buffer usage flag");

    return [&](const vk::CommandBuffer& commandBuffer)
    {
        assert(source.size() == destination.size());
        commandBuffer.copyBuffer(source.get(), destination.get(), vk::BufferCopy(0, 0, source.size()));
    };
}
