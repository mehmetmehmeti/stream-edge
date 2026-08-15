#include "streaming/GStreamerMp4Writer.h"

#include <cuda_runtime_api.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#ifdef STREAMING_HAS_GSTREAMER_CUDA
#include <cuda.h>
#include <gst/cuda/gstcudacontext.h>
#include <gst/cuda/gstcudamemory.h>
#include <gst/cuda/gstcudastream.h>
#include <gst/cuda/gstcudautils.h>
#include <gst/video/video.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

namespace streaming
{
namespace
{
std::once_flag gstInitFlag;

void ensureGStreamerInitialized()
{
    std::call_once(gstInitFlag, []() {
        gst_init(nullptr, nullptr);
#ifdef STREAMING_HAS_GSTREAMER_CUDA
        gst_cuda_memory_init_once();
#endif
    });
}

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool requireElement(GstElement* element, const char* name, std::string* errorMessage)
{
    if (element != nullptr)
        return true;

    setError(errorMessage, std::string("Missing GStreamer element: ") + name);
    return false;
}

bool elementFactoryExists(const char* name)
{
    auto* factory = gst_element_factory_find(name);
    if (factory == nullptr)
        return false;

    gst_object_unref(factory);
    return true;
}

struct EncoderPipeline
{
    const char* encoderName = nullptr;
    RecordingPath selectedPath = RecordingPath::None;
    bool useCudaMemory = false;
    bool useJetsonNvmmUpload = false;
};

std::optional<EncoderPipeline> selectGpuEncoder()
{
    constexpr EncoderPipeline kGpuEncoderCandidates[] = {
        {"nvh264enc", RecordingPath::Gpu, true, false},
        {"nvv4l2h264enc", RecordingPath::Gpu, false, true},
    };

    for (const auto& candidate : kGpuEncoderCandidates)
    {
        if (elementFactoryExists(candidate.encoderName))
            return candidate;
    }

    return std::nullopt;
}

#ifdef STREAMING_HAS_GSTREAMER_CUDA
std::string cudaDriverErrorMessage(CUresult result)
{
    const char* name = nullptr;
    const char* description = nullptr;
    (void)cuGetErrorName(result, &name);
    (void)cuGetErrorString(result, &description);

    std::ostringstream output;
    output << (name != nullptr ? name : "CUDA_ERROR_UNKNOWN");
    if (description != nullptr)
        output << ": " << description;
    return output.str();
}
#endif

void unrefElement(GstElement* element)
{
    if (element != nullptr)
        gst_object_unref(element);
}

std::string gstErrorMessage(GstMessage* message)
{
    GError* error = nullptr;
    gchar* debugInfo = nullptr;
    gst_message_parse_error(message, &error, &debugInfo);

    std::ostringstream output;
    output << (error != nullptr ? error->message : "unknown GStreamer error");
    if (debugInfo != nullptr)
        output << " (" << debugInfo << ")";

    if (error != nullptr)
        g_error_free(error);
    if (debugInfo != nullptr)
        g_free(debugInfo);

    return output.str();
}

std::size_t rgbaRowBytes(const RecordingFrame& frame)
{
    return static_cast<std::size_t>(frame.width) * 4U;
}

void setBufferTiming(GstBuffer* buffer, uint64_t ptsNs, uint64_t frameDurationNs)
{
    GST_BUFFER_PTS(buffer) = ptsNs;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = frameDurationNs;
}

bool waitForFrameReady(const RecordingFrame& frame, std::string* errorMessage)
{
    if (!frame.readySignal || !frame.readySignal->wait)
        return true;

    return frame.readySignal->wait(errorMessage);
}

bool hasProperty(GstElement* element, const char* propertyName)
{
    return element != nullptr &&
           g_object_class_find_property(G_OBJECT_GET_CLASS(element), propertyName) != nullptr;
}

void setIntPropertyIfAvailable(GstElement* element, const char* propertyName, int value)
{
    if (hasProperty(element, propertyName))
        g_object_set(element, propertyName, value, nullptr);
}

void setBoolPropertyIfAvailable(GstElement* element, const char* propertyName, bool value)
{
    if (hasProperty(element, propertyName))
        g_object_set(element, propertyName, value ? TRUE : FALSE, nullptr);
}

void configureEncoder(GstElement* encoder,
                      const EncoderPipeline& pipeline,
                      const RecordingConfig& config)
{
    auto bitrate = static_cast<int>(config.bitrateKbps);
    if (std::strcmp(pipeline.encoderName, "nvv4l2h264enc") == 0)
        bitrate = static_cast<int>(config.bitrateKbps * 1000U);

    setIntPropertyIfAvailable(encoder,
                              "bitrate",
                              bitrate);

    const auto keyFrameInterval =
        std::max(1, static_cast<int>(std::llround(config.fps)));

    if (pipeline.selectedPath == RecordingPath::Gpu)
    {
        setIntPropertyIfAvailable(encoder, "gop-size", keyFrameInterval);
        setIntPropertyIfAvailable(encoder, "iframeinterval", keyFrameInterval);
        setBoolPropertyIfAvailable(encoder, "maxperf-enable", true);
        setBoolPropertyIfAvailable(encoder, "copy-timestamp", true);
        setBoolPropertyIfAvailable(encoder, "zerolatency", true);
        return;
    }

    setIntPropertyIfAvailable(encoder, "key-int-max", keyFrameInterval);
}
} // namespace

GStreamerMp4Writer::~GStreamerMp4Writer()
{
    std::string ignored;
    (void)stop(&ignored);
}

bool GStreamerMp4Writer::start(const RecordingConfig& config, std::string* errorMessage)
{
    if (!isValid(config))
    {
        setError(errorMessage, "Invalid recording config");
        return false;
    }

    ensureGStreamerInitialized();
    clearPipeline();

    config_ = config;
    pushedFrames_ = 0;
    selectedPath_ = RecordingPath::None;
    usesCudaMemory_ = false;
    hasFirstFrameTimestamp_ = false;
    firstFrameTimestampNs_ = 0;

    const auto fpsNumerator =
        std::max<int>(1, static_cast<int>(std::llround(config.fps * 1000.0)));
    constexpr int fpsDenominator = 1000;
    frameDurationNs_ = static_cast<uint64_t>(
        (static_cast<long double>(GST_SECOND) * fpsDenominator) / fpsNumerator);

    std::string gpuError;
    if (config.mode != RecordingMode::Cpu)
    {
        const auto gpuEncoder = selectGpuEncoder();
        if (gpuEncoder && startPipeline(gpuEncoder->encoderName,
                                        gpuEncoder->selectedPath,
                                        gpuEncoder->useCudaMemory,
                                        gpuEncoder->useJetsonNvmmUpload,
                                        &gpuError))
        {
            return true;
        }

        if (config.mode == RecordingMode::Gpu)
        {
            if (!gpuEncoder)
                setError(errorMessage,
                         "GPU recording requested, but no NVIDIA H.264 encoder is available");
            else
                setError(errorMessage,
                         gpuError.empty() ? "Failed to start GPU recording pipeline" : gpuError);

            return false;
        }
    }

    std::string cpuError;
    if (startPipeline("x264enc", RecordingPath::Cpu, false, false, &cpuError))
        return true;

    if (!cpuError.empty())
        setError(errorMessage, cpuError);
    else if (!gpuError.empty())
        setError(errorMessage, gpuError);
    else
        setError(errorMessage, "Failed to start recording pipeline");

    return false;
}

bool GStreamerMp4Writer::startPipeline(const char* encoderName,
                                       RecordingPath selectedPath,
                                       bool useCudaMemory,
                                       bool useJetsonNvmmUpload,
                                       std::string* errorMessage)
{
    clearPipeline();
    usesCudaMemory_ =
#ifdef STREAMING_HAS_GSTREAMER_CUDA
        useCudaMemory;
#else
        false;
#endif

    pipeline_ = gst_pipeline_new("recording_pipeline");
    appsrc_ = gst_element_factory_make("appsrc", "recording_appsrc");
    auto* queue = gst_element_factory_make("queue", "recording_queue");
    GstElement* videoConvert = nullptr;
    GstElement* i420CapsFilter = nullptr;
    GstElement* jetsonUpload = nullptr;
    GstElement* jetsonCapsFilter = nullptr;
    const bool needsHostColorConversion = !usesCudaMemory_;
    if (needsHostColorConversion)
    {
        videoConvert = gst_element_factory_make("videoconvert", "recording_videoconvert");
        i420CapsFilter = gst_element_factory_make("capsfilter", "recording_i420_caps");
    }
    if (useJetsonNvmmUpload)
    {
        jetsonUpload = gst_element_factory_make("nvvidconv", "recording_nvvidconv");
        jetsonCapsFilter = gst_element_factory_make("capsfilter", "recording_nvmm_caps");
    }
    auto* encoder = gst_element_factory_make(encoderName, "recording_h264_encoder");
    auto* parser = gst_element_factory_make("h264parse", "recording_h264_parser");
    auto* muxer = gst_element_factory_make("mp4mux", "recording_mp4_muxer");
    auto* fileSink = gst_element_factory_make("filesink", "recording_file_sink");

    if (!requireElement(pipeline_, "pipeline", errorMessage) ||
        !requireElement(appsrc_, "appsrc", errorMessage) ||
        !requireElement(queue, "queue", errorMessage) ||
        (needsHostColorConversion &&
         !requireElement(videoConvert, "videoconvert", errorMessage)) ||
        (needsHostColorConversion &&
         !requireElement(i420CapsFilter, "capsfilter", errorMessage)) ||
        (useJetsonNvmmUpload &&
         !requireElement(jetsonUpload, "nvvidconv", errorMessage)) ||
        (useJetsonNvmmUpload &&
         !requireElement(jetsonCapsFilter, "capsfilter", errorMessage)) ||
        !requireElement(encoder, encoderName, errorMessage) ||
        !requireElement(parser, "h264parse", errorMessage) ||
        !requireElement(muxer, "mp4mux", errorMessage) ||
        !requireElement(fileSink, "filesink", errorMessage))
    {
        unrefElement(queue);
        unrefElement(videoConvert);
        unrefElement(i420CapsFilter);
        unrefElement(jetsonUpload);
        unrefElement(jetsonCapsFilter);
        unrefElement(encoder);
        unrefElement(parser);
        unrefElement(muxer);
        unrefElement(fileSink);
        unrefElement(appsrc_);
        appsrc_ = nullptr;
        clearPipeline();
        return false;
    }

    const auto fpsNumerator =
        std::max<int>(1, static_cast<int>(std::llround(config_.fps * 1000.0)));
    constexpr int fpsDenominator = 1000;

    auto* sourceCaps = gst_caps_new_simple("video/x-raw",
                                           "format", G_TYPE_STRING, "RGBA",
                                           "width", G_TYPE_INT, static_cast<int>(config_.width),
                                           "height", G_TYPE_INT, static_cast<int>(config_.height),
                                           "framerate", GST_TYPE_FRACTION,
                                           fpsNumerator,
                                           fpsDenominator,
                                           nullptr);
#ifdef STREAMING_HAS_GSTREAMER_CUDA
    if (usesCudaMemory_)
    {
        auto* cudaCapsFeatures =
            gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr);
        gst_caps_set_features(sourceCaps, 0, cudaCapsFeatures);
    }
#endif
    g_object_set(appsrc_,
                 "caps", sourceCaps,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 "block", TRUE,
                 nullptr);
    gst_caps_unref(sourceCaps);

    if (needsHostColorConversion)
    {
        auto* i420Caps = gst_caps_new_simple("video/x-raw",
                                             "format", G_TYPE_STRING, "I420",
                                             nullptr);
        g_object_set(i420CapsFilter, "caps", i420Caps, nullptr);
        gst_caps_unref(i420Caps);
    }

    if (useJetsonNvmmUpload)
    {
        auto* nvmmCaps = gst_caps_from_string("video/x-raw(memory:NVMM),format=NV12");
        g_object_set(jetsonCapsFilter, "caps", nvmmCaps, nullptr);
        gst_caps_unref(nvmmCaps);
    }

    configureEncoder(encoder,
                     {encoderName, selectedPath, usesCudaMemory_, useJetsonNvmmUpload},
                     config_);
    g_object_set(fileSink, "location", config_.outputPath.string().c_str(), nullptr);

    if (useJetsonNvmmUpload)
    {
        gst_bin_add_many(GST_BIN(pipeline_),
                         appsrc_,
                         queue,
                         videoConvert,
                         i420CapsFilter,
                         jetsonUpload,
                         jetsonCapsFilter,
                         encoder,
                         parser,
                         muxer,
                         fileSink,
                         nullptr);
    }
    else if (needsHostColorConversion)
    {
        gst_bin_add_many(GST_BIN(pipeline_),
                         appsrc_,
                         queue,
                         videoConvert,
                         i420CapsFilter,
                         encoder,
                         parser,
                         muxer,
                         fileSink,
                         nullptr);
    }
    else
    {
        gst_bin_add_many(GST_BIN(pipeline_),
                         appsrc_,
                         queue,
                         encoder,
                         parser,
                         muxer,
                         fileSink,
                         nullptr);
    }

    bool linked = false;
    if (useJetsonNvmmUpload)
    {
        linked = gst_element_link_many(appsrc_,
                                       queue,
                                       videoConvert,
                                       i420CapsFilter,
                                       jetsonUpload,
                                       jetsonCapsFilter,
                                       encoder,
                                       parser,
                                       muxer,
                                       fileSink,
                                       nullptr);
    }
    else if (needsHostColorConversion)
    {
        linked = gst_element_link_many(appsrc_,
                                       queue,
                                       videoConvert,
                                       i420CapsFilter,
                                       encoder,
                                       parser,
                                       muxer,
                                       fileSink,
                                       nullptr);
    }
    else
    {
        linked = gst_element_link_many(appsrc_,
                                       queue,
                                       encoder,
                                       parser,
                                       muxer,
                                       fileSink,
                                       nullptr);
    }

    if (!linked)
    {
        setError(errorMessage, "Failed to link GStreamer recording pipeline");
        clearPipeline();
        return false;
    }

    bus_ = gst_element_get_bus(pipeline_);

    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        setError(errorMessage, "Failed to start GStreamer recording pipeline");
        clearPipeline();
        return false;
    }

    selectedPath_ = selectedPath;
    return true;
}

bool GStreamerMp4Writer::write(const RecordingFrame& frame, std::string* errorMessage)
{
    if (pipeline_ == nullptr || appsrc_ == nullptr)
    {
        setError(errorMessage, "GStreamer recording pipeline is not active");
        return false;
    }

    const bool isRgba =
        frame.channels == 4 && frame.format == PixelFormat::RGBA8888;
    const bool isCpuRgb =
        !usesCudaMemory_ && frame.channels == 3 && frame.format == PixelFormat::RGB888;
    if (frame.width != config_.width || frame.height != config_.height ||
        (!isRgba && !isCpuRgb) || frame.dataType != FrameDataType::UInt8)
    {
        setError(errorMessage, "Recording frame does not match GStreamer writer format");
        return false;
    }

    if (usesCudaMemory_)
        return pushCudaFrame(frame, errorMessage);

    if (!waitForFrameReady(frame, errorMessage))
        return false;

    std::vector<uint8_t> hostFrame;
    if (!copyFrameToHost(frame, hostFrame, errorMessage))
        return false;

    auto* buffer = gst_buffer_new_allocate(nullptr, hostFrame.size(), nullptr);
    if (buffer == nullptr)
    {
        setError(errorMessage, "Failed to allocate GStreamer buffer");
        return false;
    }

    gst_buffer_fill(buffer, 0, hostFrame.data(), hostFrame.size());

    setBufferTiming(buffer, ptsForFrame(frame), frameDurationNs_);

    const auto flow = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (flow != GST_FLOW_OK)
    {
        setError(errorMessage, "Failed to push frame into GStreamer appsrc");
        return false;
    }

    ++pushedFrames_;
    return true;
}

bool GStreamerMp4Writer::pushCudaFrame(const RecordingFrame& frame,
                                       std::string* errorMessage)
{
#ifdef STREAMING_HAS_GSTREAMER_CUDA
    if (!waitForFrameReady(frame, errorMessage))
        return false;

    if (!frame.hasDeviceBuffer())
    {
        setError(errorMessage, "CUDA recording requires a device frame");
        return false;
    }

    const auto rowBytes = rgbaRowBytes(frame);
    if (static_cast<std::size_t>(frame.stride) < rowBytes)
    {
        setError(errorMessage, "Recording CUDA frame stride is smaller than a row");
        return false;
    }

    if (!ensureCudaContext(frame.deviceId, errorMessage))
        return false;

    GstVideoInfo videoInfo;
    if (!gst_video_info_set_format(&videoInfo,
                                   GST_VIDEO_FORMAT_RGBA,
                                   frame.width,
                                   frame.height))
    {
        setError(errorMessage, "Failed to describe CUDA recording frame format");
        return false;
    }

    GST_VIDEO_INFO_PLANE_STRIDE(&videoInfo, 0) = static_cast<int>(frame.stride);
    GST_VIDEO_INFO_SIZE(&videoInfo) =
        static_cast<gsize>(frame.stride) * static_cast<gsize>(frame.height);

    auto lifetimeToken = new std::shared_ptr<const void>(frame.lifetimeToken);
    auto* memory = gst_cuda_allocator_alloc_wrapped(
        nullptr,
        cudaContext_,
        cudaStream_,
        &videoInfo,
        reinterpret_cast<CUdeviceptr>(frame.devicePtr),
        lifetimeToken,
        [](gpointer userData) {
            delete static_cast<std::shared_ptr<const void>*>(userData);
        });

    if (memory == nullptr)
    {
        delete lifetimeToken;
        setError(errorMessage, "Failed to wrap recording frame as GStreamer CUDA memory");
        return false;
    }

    auto* buffer = gst_buffer_new();
    if (buffer == nullptr)
    {
        gst_memory_unref(memory);
        setError(errorMessage, "Failed to allocate GStreamer CUDA buffer");
        return false;
    }

    gst_buffer_append_memory(buffer, memory);
    setBufferTiming(buffer, ptsForFrame(frame), frameDurationNs_);

    const auto flow = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (flow != GST_FLOW_OK)
    {
        setError(errorMessage, "Failed to push CUDA frame into GStreamer appsrc");
        return false;
    }

    ++pushedFrames_;
    return true;
#else
    (void)frame;
    setError(errorMessage, "GStreamer CUDA support is not compiled in");
    return false;
#endif
}

bool GStreamerMp4Writer::ensureCudaContext(int32_t deviceId,
                                           std::string* errorMessage)
{
#ifdef STREAMING_HAS_GSTREAMER_CUDA
    if (cudaContext_ != nullptr && cudaStream_ != nullptr)
        return true;

    const auto selectedDeviceId = deviceId >= 0 ? deviceId : 0;
    const auto setDeviceStatus = cudaSetDevice(selectedDeviceId);
    if (setDeviceStatus != cudaSuccess)
    {
        setError(errorMessage, cudaGetErrorString(setDeviceStatus));
        return false;
    }

    const auto initStatus = cudaFree(nullptr);
    if (initStatus != cudaSuccess)
    {
        setError(errorMessage, cudaGetErrorString(initStatus));
        return false;
    }

    CUcontext currentContext = nullptr;
    auto driverStatus = cuCtxGetCurrent(&currentContext);
    if (driverStatus != CUDA_SUCCESS)
    {
        setError(errorMessage, cudaDriverErrorMessage(driverStatus));
        return false;
    }

    if (currentContext == nullptr)
    {
        setError(errorMessage, "CUDA context is not current on recording writer thread");
        return false;
    }

    CUdevice currentDevice = 0;
    driverStatus = cuCtxGetDevice(&currentDevice);
    if (driverStatus != CUDA_SUCCESS)
    {
        setError(errorMessage, cudaDriverErrorMessage(driverStatus));
        return false;
    }

    cudaContext_ = gst_cuda_context_new_wrapped(currentContext, currentDevice);
    if (cudaContext_ == nullptr)
    {
        setError(errorMessage, "Failed to wrap current CUDA context for GStreamer");
        return false;
    }

    cudaStream_ = gst_cuda_stream_new(cudaContext_);
    if (cudaStream_ == nullptr)
    {
        clearCudaContext();
        setError(errorMessage, "Failed to create GStreamer CUDA stream");
        return false;
    }

    auto* context = gst_context_new_cuda_context(cudaContext_);
    gst_element_set_context(pipeline_, context);
    gst_context_unref(context);

    return true;
#else
    (void)deviceId;
    setError(errorMessage, "GStreamer CUDA support is not compiled in");
    return false;
#endif
}

bool GStreamerMp4Writer::stop(std::string* errorMessage)
{
    if (pipeline_ == nullptr)
        return true;

    bool stopped = true;

    if (appsrc_ != nullptr)
    {
        const auto flow = gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
        if (flow != GST_FLOW_OK)
        {
            setError(errorMessage, "Failed to send EOS to GStreamer appsrc");
            stopped = false;
        }
    }

    if (bus_ != nullptr)
    {
        auto* message = gst_bus_timed_pop_filtered(
            bus_,
            10 * GST_SECOND,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

        if (message == nullptr)
        {
            setError(errorMessage, "Timed out waiting for MP4 finalization");
            stopped = false;
        }
        else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        {
            setError(errorMessage, gstErrorMessage(message));
            stopped = false;
            gst_message_unref(message);
        }
        else
        {
            gst_message_unref(message);
        }
    }

    clearPipeline();
    return stopped;
}

RecordingPath GStreamerMp4Writer::selectedPath() const
{
    return selectedPath_;
}

uint64_t GStreamerMp4Writer::ptsForFrame(const RecordingFrame& frame)
{
    if (frame.timestampNs == 0)
        return pushedFrames_ * frameDurationNs_;

    if (!hasFirstFrameTimestamp_)
    {
        firstFrameTimestampNs_ = frame.timestampNs;
        hasFirstFrameTimestamp_ = true;
        return 0;
    }

    if (frame.timestampNs <= firstFrameTimestampNs_)
        return pushedFrames_ * frameDurationNs_;

    return frame.timestampNs - firstFrameTimestampNs_;
}

bool GStreamerMp4Writer::copyFrameToHost(const RecordingFrame& frame,
                                         std::vector<uint8_t>& output,
                                         std::string* errorMessage) const
{
    const auto rowBytes = rgbaRowBytes(frame);
    const auto height = static_cast<std::size_t>(frame.height);
    output.resize(rowBytes * height);
    const auto sourcePixelBytes =
        frame.format == PixelFormat::RGB888 && frame.channels == 3 ? 3U : 4U;
    const auto sourceRowBytes =
        static_cast<std::size_t>(frame.width) * sourcePixelBytes;

    if (frame.hasHostData())
    {
        const auto sourceStride = static_cast<std::size_t>(frame.stride);
        if (sourceStride < sourceRowBytes)
        {
            setError(errorMessage, "Recording host frame stride is smaller than a row");
            return false;
        }

        if (frame.hostData.size() < sourceStride * height)
        {
            setError(errorMessage, "Recording host frame is smaller than its layout");
            return false;
        }

        if (sourcePixelBytes == 4U)
        {
            for (std::size_t row = 0; row < height; ++row)
            {
                std::memcpy(output.data() + row * rowBytes,
                            frame.hostData.data() + row * sourceStride,
                            rowBytes);
            }
            return true;
        }

        for (std::size_t row = 0; row < height; ++row)
        {
            const auto* source = frame.hostData.data() + row * sourceStride;
            auto* destination = output.data() + row * rowBytes;
            for (std::size_t pixel = 0; pixel < frame.width; ++pixel)
            {
                destination[pixel * 4U + 0U] = source[pixel * 3U + 0U];
                destination[pixel * 4U + 1U] = source[pixel * 3U + 1U];
                destination[pixel * 4U + 2U] = source[pixel * 3U + 2U];
                destination[pixel * 4U + 3U] = 255U;
            }
        }
        return true;
    }

    if (frame.hasDeviceBuffer())
    {
        if (static_cast<std::size_t>(frame.stride) < sourceRowBytes)
        {
            setError(errorMessage, "Recording device frame stride is smaller than a row");
            return false;
        }

        if (frame.deviceId >= 0)
        {
            const auto setDeviceStatus = cudaSetDevice(frame.deviceId);
            if (setDeviceStatus != cudaSuccess)
            {
                setError(errorMessage, cudaGetErrorString(setDeviceStatus));
                return false;
            }
        }

        std::vector<uint8_t> rgbFrame;
        auto* destination = output.data();
        std::size_t destinationStride = rowBytes;
        if (sourcePixelBytes == 3U)
        {
            rgbFrame.resize(sourceRowBytes * height);
            destination = rgbFrame.data();
            destinationStride = sourceRowBytes;
        }

        const auto copyStatus = cudaMemcpy2D(destination,
                                             destinationStride,
                                             frame.devicePtr,
                                             frame.stride,
                                             sourceRowBytes,
                                             height,
                                             cudaMemcpyDeviceToHost);
        if (copyStatus != cudaSuccess)
        {
            setError(errorMessage, cudaGetErrorString(copyStatus));
            return false;
        }

        if (sourcePixelBytes == 3U)
        {
            for (std::size_t pixel = 0;
                 pixel < static_cast<std::size_t>(frame.width) * height;
                 ++pixel)
            {
                output[pixel * 4U + 0U] = rgbFrame[pixel * 3U + 0U];
                output[pixel * 4U + 1U] = rgbFrame[pixel * 3U + 1U];
                output[pixel * 4U + 2U] = rgbFrame[pixel * 3U + 2U];
                output[pixel * 4U + 3U] = 255U;
            }
        }

        return true;
    }

    setError(errorMessage, "Recording frame has no readable data");
    return false;
}

void GStreamerMp4Writer::clearCudaContext()
{
    if (cudaStream_ != nullptr)
    {
#ifdef STREAMING_HAS_GSTREAMER_CUDA
        gst_cuda_stream_unref(cudaStream_);
#endif
        cudaStream_ = nullptr;
    }

    if (cudaContext_ != nullptr)
    {
        gst_object_unref(cudaContext_);
        cudaContext_ = nullptr;
    }
}

void GStreamerMp4Writer::clearPipeline()
{
    if (pipeline_ != nullptr)
        gst_element_set_state(pipeline_, GST_STATE_NULL);

    if (bus_ != nullptr)
    {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }

    if (pipeline_ != nullptr)
    {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    appsrc_ = nullptr;
    selectedPath_ = RecordingPath::None;
    usesCudaMemory_ = false;
    hasFirstFrameTimestamp_ = false;
    firstFrameTimestampNs_ = 0;
    clearCudaContext();
}

} // namespace streaming
