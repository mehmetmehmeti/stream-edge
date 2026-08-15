#include "streaming/QtOutputBridgeOp.h"

#include <dlpack/dlpack.h>
#include <holoscan/core/domain/tensor.hpp>
#include <holoscan/core/gxf/entity.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr const char* kInputPort = "input";
constexpr const char* kOutputPort = "output";

struct TensorFrameMetadata
{
    uint64_t frameNumber = 0;
    const void* devicePointer = nullptr;
    std::vector<int64_t> shape;
    std::vector<int64_t> strides;
    std::string format;
    std::string dataType;
    std::string device;
    int32_t deviceId = -1;
    int64_t width = 0;
    int64_t height = 0;
    int64_t channels = 0;
    int64_t rowStrideBytes = 0;
    int64_t byteCount = 0;
    bool isUint8 = false;
    bool isCudaDeviceMemory = false;
    bool isRgb8Tensor = false;
    bool isRgba8Tensor = false;
    bool isSupportedColorTensor = false;
    bool hasValidLayout = false;
    bool isSupportedFrameCandidate = false;
};

std::string joinValues(const std::vector<int64_t>& values)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
            output << "x";
        output << values[index];
    }

    return output.str();
}

std::string dataTypeCodeToString(uint8_t code)
{
    switch (code)
    {
    case kDLInt:
        return "int";
    case kDLUInt:
        return "uint";
    case kDLFloat:
        return "float";
    case kDLBfloat:
        return "bfloat";
    case kDLComplex:
        return "complex";
    case kDLBool:
        return "bool";
    default:
        return "unknown";
    }
}

std::string dataTypeToString(const DLDataType& dataType)
{
    std::ostringstream output;
    output << dataTypeCodeToString(dataType.code) << static_cast<int>(dataType.bits);
    if (dataType.lanes > 1)
        output << "x" << static_cast<int>(dataType.lanes);

    return output.str();
}

std::string deviceTypeToString(DLDeviceType deviceType)
{
    switch (deviceType)
    {
    case kDLCPU:
        return "cpu";
    case kDLCUDA:
        return "cuda";
    case kDLCUDAHost:
        return "cuda_host";
    case kDLCUDAManaged:
        return "cuda_managed";
    case kDLVulkan:
        return "vulkan";
    default:
        return "unknown";
    }
}

std::string deviceToString(const DLDevice& device)
{
    std::ostringstream output;
    output << deviceTypeToString(device.device_type) << ":" << device.device_id;
    return output.str();
}

int64_t elementSizeBytes(const DLDataType& dataType)
{
    return (static_cast<int64_t>(dataType.bits) * dataType.lanes + 7) / 8;
}

TensorFrameMetadata describeTensor(const holoscan::Tensor& tensor, uint64_t frameNumber)
{
    TensorFrameMetadata metadata;
    const auto dataType = tensor.dtype();

    metadata.frameNumber = frameNumber;
    metadata.devicePointer = tensor.data();
    metadata.shape = tensor.shape();
    metadata.strides = tensor.strides();
    metadata.dataType = dataTypeToString(dataType);
    metadata.device = deviceToString(tensor.device());
    metadata.deviceId = tensor.device().device_id;
    metadata.byteCount = tensor.nbytes();
    metadata.isUint8 = dataType.code == kDLUInt && dataType.bits == 8 && dataType.lanes == 1;
    metadata.isCudaDeviceMemory = tensor.device().device_type == kDLCUDA;

    if (metadata.shape.size() >= 2)
    {
        metadata.height = metadata.shape[0];
        metadata.width = metadata.shape[1];
    }

    if (!metadata.shape.empty())
        metadata.channels = metadata.shape.back();

    if (!metadata.strides.empty())
        metadata.rowStrideBytes = metadata.strides[0] * elementSizeBytes(dataType);
    else if (metadata.width > 0 && metadata.channels > 0)
        metadata.rowStrideBytes = metadata.width * metadata.channels * elementSizeBytes(dataType);

    const bool is3dUint8Tensor = metadata.shape.size() == 3 && metadata.isUint8;
    metadata.isRgb8Tensor = is3dUint8Tensor && metadata.channels == 3;
    metadata.isRgba8Tensor = is3dUint8Tensor && metadata.channels == 4;
    metadata.isSupportedColorTensor = metadata.isRgb8Tensor || metadata.isRgba8Tensor;
    metadata.hasValidLayout = metadata.width > 0 && metadata.height > 0 &&
                              metadata.channels > 0 && metadata.rowStrideBytes > 0 &&
                              metadata.byteCount > 0 &&
                              metadata.rowStrideBytes >= metadata.width * metadata.channels;

    if (metadata.isRgb8Tensor)
        metadata.format = "rgb888";
    else if (metadata.isRgba8Tensor)
        metadata.format = "rgba8888";
    else
        metadata.format = "unsupported";

    metadata.isSupportedFrameCandidate = metadata.isSupportedColorTensor &&
                                         metadata.hasValidLayout &&
                                         metadata.isCudaDeviceMemory &&
                                         metadata.devicePointer != nullptr;

    return metadata;
}

void logValidationWarnings(const TensorFrameMetadata& metadata)
{
    if (metadata.devicePointer == nullptr)
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp received tensor with null data pointer");

    if (metadata.shape.size() != 3)
    {
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp expected a 3D RGB/RGBA tensor, got shape [{}]",
                          joinValues(metadata.shape));
    }
    else if (metadata.channels != 3 && metadata.channels != 4)
    {
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp expected RGB/RGBA input with 3 or 4 channels, got {} channel(s)",
                          metadata.channels);
    }

    if (!metadata.isUint8)
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp expected uint8 RGB/RGBA input, got {}", metadata.dataType);

    if (!metadata.hasValidLayout)
    {
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp received invalid tensor layout: width={}, "
                          "height={}, channels={}, row_stride_bytes={}, bytes={}",
                          metadata.width,
                          metadata.height,
                          metadata.channels,
                          metadata.rowStrideBytes,
                          metadata.byteCount);
    }

    if (!metadata.isCudaDeviceMemory)
    {
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp expected CUDA device memory for CUDA/OpenGL "
                          "interop, got {}",
                          metadata.device);
    }
}

void logFrameMetadata(const TensorFrameMetadata& metadata)
{
    HOLOSCAN_LOG_INFO(
        "QtOutputBridgeOp frame {} metadata: {}x{} channels={}, shape=[{}], strides=[{}], "
        "format={}, dtype={}, device={}, ptr={}, bytes={}, row_stride_bytes={}",
        metadata.frameNumber,
        metadata.width,
        metadata.height,
        metadata.channels,
        joinValues(metadata.shape),
        joinValues(metadata.strides),
        metadata.format,
        metadata.dataType,
        metadata.device,
        metadata.devicePointer,
        metadata.byteCount,
        metadata.rowStrideBytes);
}

streaming::PixelFormat pixelFormatFromMetadata(const TensorFrameMetadata& metadata)
{
    if (metadata.isRgb8Tensor)
        return streaming::PixelFormat::RGB888;

    if (metadata.isRgba8Tensor)
        return streaming::PixelFormat::RGBA8888;

    return streaming::PixelFormat::Unknown;
}

uint32_t clampToUint32(int64_t value)
{
    if (value <= 0)
        return 0;

    if (value > std::numeric_limits<uint32_t>::max())
        return std::numeric_limits<uint32_t>::max();

    return static_cast<uint32_t>(value);
}

uint64_t currentTimestampNs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}
} // namespace

QtOutputBridgeOp::QtOutputBridgeOp()
{
}

std::shared_ptr<streaming::FrameBridge> QtOutputBridgeOp::sharedFrameBridge()
{
    return defaultFrameBridge();
}

std::shared_ptr<streaming::FrameBridge> QtOutputBridgeOp::defaultFrameBridge()
{
    static auto bridge = std::make_shared<streaming::FrameBridge>();
    return bridge;
}

void QtOutputBridgeOp::setup(holoscan::OperatorSpec& spec)
{
    spec.input<nvidia::gxf::Entity>(kInputPort);
    spec.output<nvidia::gxf::Entity>(kOutputPort);
}

void QtOutputBridgeOp::compute(holoscan::InputContext& opInput,
                               holoscan::OutputContext& opOutput,
                               holoscan::ExecutionContext& context)
{
    (void)context;

    auto maybeEntity = opInput.receive<nvidia::gxf::Entity>(kInputPort);
    if (!maybeEntity)
    {
        HOLOSCAN_LOG_ERROR("QtOutputBridgeOp failed to receive frame: {}",
                           maybeEntity.error().what());
        return;
    }

    ++receivedFrameCount_;

    auto entity = maybeEntity.value();
    if (entity.is_null())
    {
        ++invalidFrameCount_;
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp received an empty frame entity");
        return;
    }

    holoscan::gxf::Entity frameEntity(entity);
    auto tensor = frameEntity.get<holoscan::Tensor>(nullptr, false);
    if (!tensor)
    {
        ++invalidFrameCount_;
        HOLOSCAN_LOG_WARN("QtOutputBridgeOp received a frame entity without a tensor component");
        opOutput.emit(entity, kOutputPort);
        return;
    }

    const auto metadata = describeTensor(*tensor, receivedFrameCount_);
    const bool metadataChanged = !hasLoggedFrameMetadata_ || metadata.shape != lastShape_ ||
                                 metadata.strides != lastStrides_ ||
                                 metadata.dataType != lastDataType_ ||
                                 metadata.device != lastDevice_;

    if (metadataChanged)
    {
        logFrameMetadata(metadata);
        logValidationWarnings(metadata);

        hasLoggedFrameMetadata_ = true;
        lastShape_ = metadata.shape;
        lastStrides_ = metadata.strides;
        lastDataType_ = metadata.dataType;
        lastDevice_ = metadata.device;
    }

    if (!metadata.isSupportedFrameCandidate)
        ++invalidFrameCount_;
    else
    {
        const auto packedRowBytes = static_cast<uint64_t>(metadata.width * metadata.channels);
        const auto packedByteCount = packedRowBytes * static_cast<uint64_t>(metadata.height);
        auto lease = stagingPool_->acquire(packedByteCount);
        if (!lease)
        {
            ++droppedFrameCount_;
            HOLOSCAN_LOG_WARN(
                "QtOutputBridgeOp dropped frame {} because no staging slot is available",
                metadata.frameNumber);
        }
        else
        {
            auto copyStatus = stagingPool_->copy2DFromDevice(
                *lease,
                metadata.devicePointer,
                static_cast<uint64_t>(metadata.rowStrideBytes),
                packedRowBytes,
                static_cast<uint64_t>(metadata.height));
            if (copyStatus != cudaSuccess)
            {
                ++invalidFrameCount_;
                HOLOSCAN_LOG_WARN("QtOutputBridgeOp failed to copy frame {} into staging "
                                  "memory: {}",
                                  metadata.frameNumber,
                                  cudaGetErrorString(copyStatus));
            }
            else
            {
                auto lifetimeToken =
                    std::shared_ptr<const void>(lease, static_cast<const void*>(lease.get()));
                streaming::VideoFrameHandle frame{
                    .width = clampToUint32(metadata.width),
                    .height = clampToUint32(metadata.height),
                    .stride = clampToUint32(static_cast<int64_t>(packedRowBytes)),
                    .channels = clampToUint32(metadata.channels),
                    .byteCount = packedByteCount,
                    .format = pixelFormatFromMetadata(metadata),
                    .dataType = streaming::FrameDataType::UInt8,
                    .memoryLocation = streaming::MemoryLocation::Device,
                    .deviceId = metadata.deviceId,
                    .devicePtr = lease->devicePtr(),
                    .frameNumber = metadata.frameNumber,
                    .timestampNs = currentTimestampNs(),
                    .lifetimeToken = std::move(lifetimeToken),
                };

                frameBridge_->publishFrame(frame);
                ++stagedFrameCount_;
            }
        }
    }

    HOLOSCAN_LOG_DEBUG("QtOutputBridgeOp received frame {}: ptr={}, bytes={}, staged_frames={}, "
                       "dropped_frames={}, invalid_frames={}",
                       metadata.frameNumber,
                       metadata.devicePointer,
                       metadata.byteCount,
                       stagedFrameCount_,
                       droppedFrameCount_,
                       invalidFrameCount_);

    opOutput.emit(entity, kOutputPort);
}
