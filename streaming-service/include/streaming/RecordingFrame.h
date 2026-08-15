#pragma once

#include "streaming/VideoFrameHandle.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace streaming
{

struct RecordingFrameReadySignal
{
    std::function<bool(std::string*)> wait;
};

struct RecordingFrame
{
    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride{0};
    uint32_t channels{0};
    uint64_t byteCount{0};
    PixelFormat format{PixelFormat::Unknown};
    FrameDataType dataType{FrameDataType::Unknown};
    MemoryLocation memoryLocation{MemoryLocation::Unknown};
    int32_t deviceId{-1};
    const void* devicePtr{nullptr};
    std::vector<uint8_t> hostData;
    uint64_t frameNumber{0};
    uint64_t timestampNs{0};
    std::shared_ptr<const void> lifetimeToken{};
    std::shared_ptr<RecordingFrameReadySignal> readySignal{};

    bool hasHostData() const
    {
        return memoryLocation == MemoryLocation::Host && !hostData.empty();
    }

    bool hasDeviceBuffer() const
    {
        return memoryLocation == MemoryLocation::Device && devicePtr != nullptr;
    }
};

} // namespace streaming
