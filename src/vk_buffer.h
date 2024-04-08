#pragma once

#include "vk_types.h"
#include "vk_command.h"
#include "vk_device.h"

#include <chrono>
#include <limits>
#include <numeric>

enum class VulkanBufferType
{
    DeviceLocal,
    Staging,
    HostAccessible
};

consteval vk::MemoryPropertyFlags getMemoryFlags(VulkanBufferType type) noexcept
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
    vk::DeviceSize bufferSize_;
    vk::raii::Buffer buffer_;
    vk::DeviceSize bufferCapacity_ = 0;
    vk::raii::DeviceMemory bufferMemory_;

    constexpr static vk::MemoryPropertyFlags memoryPropertyFlags_ = getMemoryFlags(bufferType);

    vk::raii::DeviceMemory createBufferMemory(const VulkanDevice& device)
    {
        vk::MemoryRequirements memoryRequirements = buffer_.getMemoryRequirements();
        const vk::PhysicalDeviceMemoryProperties memoryProperties = device.physicalDevice.getMemoryProperties();
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
        return device.device.allocateMemory({ bufferCapacity_, findMemoryType(memoryRequirements, memoryProperties) });
    }
public:
    VulkanBuffer(const VulkanDevice& device, vk::DeviceSize bufferSize) :
        bufferSize_(bufferSize),
        buffer_(device.device.createBuffer(vk::BufferCreateInfo({}, bufferSize, usage, vk::SharingMode::eExclusive, {}))),
        bufferMemory_(createBufferMemory(device))
    {
        buffer_.bindMemory(*bufferMemory_, 0);
    }
    DECLARE_CONSTRUCTORS_MOVE_DEFAULTED(VulkanBuffer)

    const vk::Buffer& get() const noexcept { return *buffer_; }
    constexpr vk::DeviceSize size() const { return bufferSize_; }
    constexpr vk::DeviceSize capacity() const { return bufferCapacity_; }
    template <typename T, size_t N>
    void copyFrom(const gsl::span<const T, N> data) const
    {
        static_assert(bufferType == VulkanBufferType::Staging);
        assert(data.size_bytes() <= bufferSize_);
        void* bufferPointer = bufferMemory_.mapMemory(0, data.size_bytes());
        std::memcpy(bufferPointer, data.data(), data.size_bytes());
        bufferMemory_.unmapMemory();
    }
    template <typename T, size_t N>
    void copyTo(const gsl::span<T, N> data) const
    {
        static_assert(bufferType == VulkanBufferType::Staging);
        assert(data.size_bytes() >= bufferSize_);
        const T* bufferPointer = reinterpret_cast<T*>(bufferMemory_.mapMemory(0, bufferSize_));
        std::memcpy(data.data(), bufferPointer, data.size_bytes());
        bufferMemory_.unmapMemory();
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
