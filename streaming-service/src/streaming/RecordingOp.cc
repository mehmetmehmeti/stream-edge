#include "streaming/RecordingOp.h"

#ifdef STREAMING_HAS_GSTREAMER
#include "streaming/GStreamerMp4Writer.h"
#endif

#include <cuda_runtime_api.h>
#include <dlpack/dlpack.h>
#include <holoscan/core/domain/tensor.hpp>
#include <holoscan/core/gxf/entity.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr const char* kInputPort = "input";

struct CudaEventToken
{
    explicit CudaEventToken(cudaEvent_t event)
        : event(event)
    {
    }

    ~CudaEventToken()
    {
        if (event != nullptr)
            (void)cudaEventDestroy(event);
    }

    cudaEvent_t event{nullptr};
};

struct TensorFrameMetadata
{
    uint64_t frameNumber = 0;
    const void* dataPointer = nullptr;
    std::vector<int64_t> shape;
    std::vector<int64_t> strides;
    int32_t deviceId = -1;
    int64_t width = 0;
    int64_t height = 0;
    int64_t channels = 0;
    int64_t rowStrideBytes = 0;
    int64_t byteCount = 0;
    bool isUint8 = false;
    bool isCudaDeviceMemory = false;
    bool isCpuMemory = false;
    bool isRgb8Tensor = false;
    bool isRgba8Tensor = false;
    bool hasValidLayout = false;
};

int64_t elementSizeBytes(const DLDataType& dataType)
{
    return (static_cast<int64_t>(dataType.bits) * dataType.lanes + 7) / 8;
}

TensorFrameMetadata describeTensor(const holoscan::Tensor& tensor, uint64_t frameNumber)
{
    TensorFrameMetadata metadata;
    const auto dataType = tensor.dtype();

    metadata.frameNumber = frameNumber;
    metadata.dataPointer = tensor.data();
    metadata.shape = tensor.shape();
    metadata.strides = tensor.strides();
    metadata.deviceId = tensor.device().device_id;
    metadata.byteCount = tensor.nbytes();
    metadata.isUint8 = dataType.code == kDLUInt && dataType.bits == 8 && dataType.lanes == 1;
    metadata.isCudaDeviceMemory = tensor.device().device_type == kDLCUDA;
    metadata.isCpuMemory = tensor.device().device_type == kDLCPU;

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
    metadata.hasValidLayout = metadata.width > 0 && metadata.height > 0 &&
                              metadata.channels > 0 && metadata.rowStrideBytes > 0 &&
                              metadata.byteCount > 0 && metadata.dataPointer != nullptr;

    return metadata;
}

bool isSupportedRecordingTensor(const TensorFrameMetadata& metadata)
{
    return metadata.hasValidLayout &&
           (metadata.isRgb8Tensor || metadata.isRgba8Tensor) &&
           (metadata.isCudaDeviceMemory || metadata.isCpuMemory);
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

streaming::RecordingFrame makeBaseFrame(const TensorFrameMetadata& metadata)
{
    streaming::RecordingFrame frame;
    frame.width = clampToUint32(metadata.width);
    frame.height = clampToUint32(metadata.height);
    frame.stride = clampToUint32(metadata.rowStrideBytes);
    frame.channels = clampToUint32(metadata.channels);
    frame.byteCount = static_cast<uint64_t>(metadata.byteCount);
    frame.format = pixelFormatFromMetadata(metadata);
    frame.dataType = streaming::FrameDataType::UInt8;
    frame.deviceId = metadata.deviceId;
    frame.frameNumber = metadata.frameNumber;
    frame.timestampNs = currentTimestampNs();
    return frame;
}

bool copyHostFrame(const TensorFrameMetadata& metadata, streaming::RecordingFrame& frame)
{
    const auto rowBytes =
        static_cast<std::size_t>(metadata.width * metadata.channels);
    const auto height = static_cast<std::size_t>(metadata.height);
    const auto sourceStride = static_cast<std::size_t>(metadata.rowStrideBytes);
    const auto* source = static_cast<const uint8_t*>(metadata.dataPointer);

    frame.hostData.resize(rowBytes * height);
    for (std::size_t row = 0; row < height; ++row)
    {
        std::memcpy(frame.hostData.data() + row * rowBytes,
                    source + row * sourceStride,
                    rowBytes);
    }

    frame.stride = static_cast<uint32_t>(rowBytes);
    frame.byteCount = static_cast<uint64_t>(frame.hostData.size());
    frame.memoryLocation = streaming::MemoryLocation::Host;
    return true;
}

std::shared_ptr<streaming::RecordingFrameReadySignal> makeReadySignal(
    std::shared_ptr<CudaEventToken> eventToken)
{
    auto signal = std::make_shared<streaming::RecordingFrameReadySignal>();
    signal->wait = [eventToken = std::move(eventToken)](std::string* errorMessage) {
        const auto status = cudaEventSynchronize(eventToken->event);
        if (status == cudaSuccess)
            return true;

        if (errorMessage)
            *errorMessage = cudaGetErrorString(status);
        return false;
    };
    return signal;
}
} // namespace

RecordingOp::RecordingOp()
{
}

RecordingOp::~RecordingOp()
{
    if (stagingStream_ != nullptr)
        (void)cudaStreamDestroy(stagingStream_);
}

std::shared_ptr<streaming::RecordingEngine> RecordingOp::sharedRecordingEngine()
{
    return defaultRecordingEngine();
}

std::shared_ptr<streaming::RecordingEngine> RecordingOp::defaultRecordingEngine()
{
    static auto engine = std::make_shared<streaming::RecordingEngine>(
#ifdef STREAMING_HAS_GSTREAMER
        std::make_unique<streaming::GStreamerMp4Writer>()
#else
        std::unique_ptr<streaming::RecordingWriter>{}
#endif
    );
    return engine;
}

void RecordingOp::setup(holoscan::OperatorSpec& spec)
{
    spec.input<nvidia::gxf::Entity>(kInputPort);
}

cudaStream_t RecordingOp::stagingStream()
{
    if (stagingStream_ != nullptr)
        return stagingStream_;

    const auto status = cudaStreamCreateWithFlags(&stagingStream_, cudaStreamNonBlocking);
    if (status != cudaSuccess)
    {
        HOLOSCAN_LOG_WARN("RecordingOp failed to create async staging stream: {}",
                          cudaGetErrorString(status));
        stagingStream_ = nullptr;
    }

    return stagingStream_;
}

void RecordingOp::compute(holoscan::InputContext& opInput,
                          holoscan::OutputContext& opOutput,
                          holoscan::ExecutionContext& context)
{
    (void)opOutput;
    (void)context;

    auto maybeEntity = opInput.receive<nvidia::gxf::Entity>(kInputPort);
    if (!maybeEntity)
    {
        HOLOSCAN_LOG_ERROR("RecordingOp failed to receive frame: {}",
                           maybeEntity.error().what());
        return;
    }

    ++receivedFrameCount_;

    if (!recordingEngine_ || !recordingEngine_->isActive())
        return;

    auto entity = maybeEntity.value();
    if (entity.is_null())
        return;

    holoscan::gxf::Entity frameEntity(entity);
    auto tensor = frameEntity.get<holoscan::Tensor>(nullptr, false);
    if (!tensor)
        return;

    const auto metadata = describeTensor(*tensor, receivedFrameCount_);
    if (!isSupportedRecordingTensor(metadata))
        return;

    auto frame = makeBaseFrame(metadata);
    if (metadata.isCpuMemory)
    {
        if (!copyHostFrame(metadata, frame))
            return;
    }
    else
    {
        auto lease = stagingPool_->acquire(static_cast<uint64_t>(metadata.byteCount));
        if (!lease)
            return;

        cudaEvent_t readyEvent = nullptr;
        auto copyStream = stagingStream();
        auto eventStatus = copyStream != nullptr
                               ? cudaEventCreateWithFlags(&readyEvent, cudaEventDisableTiming)
                               : cudaErrorInvalidResourceHandle;

        auto copyStatus = eventStatus == cudaSuccess
                              ? stagingPool_->copyFromDeviceAsync(
                                    *lease,
                                    metadata.dataPointer,
                                    static_cast<uint64_t>(metadata.byteCount),
                                    copyStream)
                              : cudaErrorInvalidResourceHandle;
        const bool copyWasQueued = copyStatus == cudaSuccess;
        if (copyStatus == cudaSuccess)
        {
            const auto recordStatus = cudaEventRecord(readyEvent, copyStream);
            if (recordStatus != cudaSuccess)
            {
                copyStatus = cudaStreamSynchronize(copyStream);
                (void)cudaEventDestroy(readyEvent);
                readyEvent = nullptr;
            }
        }
        else if (readyEvent != nullptr)
        {
            (void)cudaEventDestroy(readyEvent);
            readyEvent = nullptr;
        }

        if (copyStatus != cudaSuccess && !copyWasQueued)
        {
            copyStatus = stagingPool_->copyFromDevice(
                *lease, metadata.dataPointer, static_cast<uint64_t>(metadata.byteCount));
        }

        if (copyStatus != cudaSuccess)
        {
            HOLOSCAN_LOG_WARN("RecordingOp failed to stage frame {}: {}",
                              metadata.frameNumber,
                              cudaGetErrorString(copyStatus));
            return;
        }

        frame.memoryLocation = streaming::MemoryLocation::Device;
        frame.devicePtr = lease->devicePtr();
        frame.lifetimeToken =
            std::shared_ptr<const void>(lease, static_cast<const void*>(lease.get()));
        if (readyEvent != nullptr)
            frame.readySignal = makeReadySignal(std::make_shared<CudaEventToken>(readyEvent));
    }

    (void)recordingEngine_->trySubmit(std::move(frame));
}
