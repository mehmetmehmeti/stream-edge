#pragma once

#include <cstdint>
#include <memory>

namespace streaming
{

enum class PixelFormat
{
    RGB888,
    RGBA8888,
    Unknown
};

enum class MemoryLocation
{
    Device,
    Host,
    Unknown
};

enum class FrameDataType
{
    UInt8,
    Unknown
};

struct VideoFrameHandle
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
    uint64_t frameNumber{0};
    uint64_t timestampNs{0};
    std::shared_ptr<const void> lifetimeToken{};

    bool hasDeviceBuffer() const
    {
        return memoryLocation == MemoryLocation::Device && devicePtr != nullptr;
    }

    bool hasLifetimeToken() const
    {
        return static_cast<bool>(lifetimeToken);
    }

    bool isSupportedFormat() const
    {
        return format == PixelFormat::RGB888 || format == PixelFormat::RGBA8888;
    }
};

} // namespace streaming
