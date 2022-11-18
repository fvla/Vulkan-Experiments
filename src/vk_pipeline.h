#pragma once

#include "vk_types.h"
#include "vk_sync.h"
#include "vk_command.h"

/* This concept describes GPU processes which have an array of 0 or more semaphores which fire
   when the process ends, and an accessor for 0 or more fences to signal the CPU with. Pipelines
   with at least one signal semaphore can be set to trigger pipelines that accept semaphores
   to wait on. Tail pipelines have no signal semaphores, and head pipelines do not accept waiting
   semaphores. */
template <typename T>
concept VulkanPipeline = requires(T a)
{
    ArrayOf<decltype(a), vk::Semaphore>;
};
//const std::array<double, 0>& aaaaa() {}
//template <typename T>
//concept VVVVV = requires { ArrayOf<std::remove_reference_t<decltype(aaaaa())>, T>; };
//static_assert(VVVVV<int>);

template <VulkanPipeline... Args>
class SeriesPipeline
{
    std::tuple<Args...> pipelineItems_;
public:
    SeriesPipeline(Args...&& args)
        : pipelineItems_(std::forward(args)...)
    {

    }
};

template <VulkanCommandRecorder Recorder, Void V = void>
class CommandHandlerPipelineModule
{
    VulkanSemaphore<V> signalSemaphore_;
    VulkanCommandHandler<V>& commandHandler_;
    Recorder commandRecorder_;
public:
    CommandHandlerPipelineModule(const vk::Device& device, VulkanCommandHandler<V>& commandHandler, Recorder&& recorder)
        : signalSemaphore_(device), commandHandler_(commandHandler), commandRecorder_(std::forward(recorder))
    {
    }
};
