#include "services/FrameBridgeShmPublisher.h"

#include <cuda_runtime_api.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace
{
std::once_flag gstreamerInitFlag;
bool gstreamerInitialized = false;
std::string gstreamerInitError;

bool initGstreamer(std::string& error)
{
    std::call_once(gstreamerInitFlag, [&]() {
        GError* gstError = nullptr;
        if (!gst_init_check(nullptr, nullptr, &gstError))
        {
            if (gstError != nullptr)
            {
                gstreamerInitError = gstError->message != nullptr ? gstError->message
                                                                  : "Unknown GStreamer init error";
                g_error_free(gstError);
            }
            else
            {
                gstreamerInitError = "Unknown GStreamer init error";
            }
            return;
        }

        gstreamerInitialized = true;
    });

    if (!gstreamerInitialized && !gstreamerInitError.empty())
    {
        error = gstreamerInitError;
        return false;
    }

    if (!gst_is_initialized())
    {
        error = "GStreamer initialization failed";
        return false;
    }

    return true;
}

std::uint64_t packedRowBytes(const streaming::VideoFrameHandle& frame)
{
    return static_cast<std::uint64_t>(frame.width) * static_cast<std::uint64_t>(frame.channels);
}

void setObjectPropertyIfAvailable(GObject* object, const char* name, gint value)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(object), name) != nullptr)
        g_object_set(object, name, value, nullptr);
}

bool checkPipelineHealth(GstElement* pipeline, std::string& error)
{
    if (pipeline == nullptr)
        return true;

    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus == nullptr)
        return true;

    GstMessage* message = gst_bus_pop_filtered(
        bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    gst_object_unref(bus);
    if (message == nullptr)
        return true;

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    {
        GError* gstError = nullptr;
        gchar* debugDetails = nullptr;
        gst_message_parse_error(message, &gstError, &debugDetails);
        error = "GStreamer shared-memory pipeline failed";
        if (gstError != nullptr && gstError->message != nullptr)
            error += std::string(": ") + gstError->message;
        if (gstError != nullptr)
            g_error_free(gstError);
        g_free(debugDetails);
    }
    else
    {
        error = "GStreamer shared-memory pipeline reached end of stream unexpectedly";
    }

    gst_message_unref(message);
    return false;
}

GstClockTime configuredFrameDuration(const FrameBridgeShmPublisher::Config& config)
{
    if (config.frameRateNumerator == 0 || config.frameRateDenominator == 0)
        return GST_CLOCK_TIME_NONE;

    return gst_util_uint64_scale(
        GST_SECOND,
        static_cast<guint64>(config.frameRateDenominator),
        static_cast<guint64>(config.frameRateNumerator));
}
} // namespace

FrameBridgeShmPublisher::~FrameBridgeShmPublisher()
{
    stop();
}

bool FrameBridgeShmPublisher::start(const Config& config, std::string& error)
{
    if (!initGstreamer(error))
        return false;

    std::lock_guard lock(mutex_);
    cleanupUnlocked();

    config_ = config;
    streamInfo_.reset();
    framesPushed_ = 0;
    bytesPushed_ = 0;
    lastFrameNumber_ = 0;
    firstTimestampNs_ = 0;
    lastTimestampNs_ = 0;
    lastBufferPtsNs_ = 0;
    haveLastBufferPts_ = false;
    lastError_.clear();
    started_ = true;
    return true;
}

bool FrameBridgeShmPublisher::pushFrame(const streaming::VideoFrameHandle& frame, std::string& error)
{
    std::lock_guard lock(mutex_);

    if (!started_)
    {
        error = "Frame bridge publisher is not started";
        lastError_ = error;
        return false;
    }

    if (!frame.hasDeviceBuffer() || !frame.isSupportedFormat())
    {
        error = "Unsupported frame received for shared-memory publishing";
        lastError_ = error;
        return false;
    }

    if (!ensurePipelineForFrame(frame, error))
    {
        lastError_ = error;
        return false;
    }

    if (!checkPipelineHealth(pipeline_, error))
    {
        lastError_ = error;
        return false;
    }

    const auto rowBytes = packedRowBytes(frame);
    const auto totalBytes = rowBytes * static_cast<std::uint64_t>(frame.height);

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, static_cast<gsize>(totalBytes), nullptr);
    if (buffer == nullptr)
    {
        error = "Failed to allocate GStreamer buffer";
        lastError_ = error;
        return false;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE))
    {
        gst_buffer_unref(buffer);
        error = "Failed to map GStreamer buffer";
        lastError_ = error;
        return false;
    }

    const auto copyStatus = cudaMemcpy2D(map.data,
                                         static_cast<std::size_t>(rowBytes),
                                         frame.devicePtr,
                                         static_cast<std::size_t>(frame.stride),
                                         static_cast<std::size_t>(rowBytes),
                                         static_cast<std::size_t>(frame.height),
                                         cudaMemcpyDeviceToHost);
    gst_buffer_unmap(buffer, &map);
    if (copyStatus != cudaSuccess)
    {
        gst_buffer_unref(buffer);
        error = std::string("Failed to copy frame from CUDA device: ") +
                cudaGetErrorString(copyStatus);
        lastError_ = error;
        return false;
    }

    if (firstTimestampNs_ == 0)
        firstTimestampNs_ = frame.timestampNs;

    const auto relativeTimestampNs = frame.timestampNs >= firstTimestampNs_
                                         ? frame.timestampNs - firstTimestampNs_
                                         : 0;
    const auto duration = configuredFrameDuration(config_);
    auto pts = static_cast<GstClockTime>(relativeTimestampNs);
    if (haveLastBufferPts_ && pts <= lastBufferPtsNs_)
    {
        pts = lastBufferPtsNs_ + (duration != GST_CLOCK_TIME_NONE ? duration : 1);
    }

    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = duration;

    const auto pushResult = gst_app_src_push_buffer(appSrc_, buffer);
    if (pushResult != GST_FLOW_OK)
    {
        error = "Failed to push frame into GStreamer appsrc";
        lastError_ = error;
        return false;
    }

    if (!checkPipelineHealth(pipeline_, error))
    {
        lastError_ = error;
        return false;
    }

    ++framesPushed_;
    bytesPushed_ += totalBytes;
    lastFrameNumber_ = frame.frameNumber;
    lastTimestampNs_ = frame.timestampNs;
    lastBufferPtsNs_ = static_cast<std::uint64_t>(pts);
    haveLastBufferPts_ = true;
    return true;
}

void FrameBridgeShmPublisher::stop()
{
    std::lock_guard lock(mutex_);
    cleanupUnlocked();
}

bool FrameBridgeShmPublisher::isRunning() const
{
    std::lock_guard lock(mutex_);
    return started_;
}

Json::Value FrameBridgeShmPublisher::statusJson() const
{
    std::lock_guard lock(mutex_);

    Json::Value json;
    json["started"] = started_;
    json["frames_pushed"] = Json::UInt64(framesPushed_);
    json["bytes_pushed"] = Json::UInt64(bytesPushed_);
    json["last_frame_number"] = Json::UInt64(lastFrameNumber_);
    json["last_frame_timestamp_ns"] = Json::UInt64(lastTimestampNs_);
    json["socket_path"] = config_.socketPath;
    json["frame_rate_num"] = config_.frameRateNumerator;
    json["frame_rate_den"] = config_.frameRateDenominator;

    if (framesPushed_ > 1 && lastTimestampNs_ > firstTimestampNs_)
    {
        const auto elapsedNs = static_cast<double>(lastTimestampNs_ - firstTimestampNs_);
        json["effective_fps"] =
            static_cast<double>(framesPushed_ - 1) * 1000000000.0 / elapsedNs;
    }

    if (!lastError_.empty())
        json["last_error"] = lastError_;

    if (streamInfo_.has_value())
    {
        json["width"] = streamInfo_->width;
        json["height"] = streamInfo_->height;
        json["channels"] = streamInfo_->channels;
        json["gst_format"] = streamInfo_->gstFormat;
    }

    return json;
}

std::optional<FrameBridgeShmPublisher::StreamInfo> FrameBridgeShmPublisher::streamInfo() const
{
    std::lock_guard lock(mutex_);
    return streamInfo_;
}

bool FrameBridgeShmPublisher::ensurePipelineForFrame(const streaming::VideoFrameHandle& frame,
                                                     std::string& error)
{
    if (streamInfo_.has_value())
    {
        if (streamInfo_->width != frame.width || streamInfo_->height != frame.height ||
            streamInfo_->channels != frame.channels)
        {
            error = "Frame dimensions changed during publish session";
            return false;
        }
        return true;
    }

    auto gstFormat = gstFormatForFrame(frame);
    if (!gstFormat.has_value())
    {
        error = "Unsupported pixel format for shared-memory publishing";
        return false;
    }

    StreamInfo streamInfo;
    streamInfo.socketPath = config_.socketPath;
    streamInfo.gstFormat = *gstFormat;
    streamInfo.width = frame.width;
    streamInfo.height = frame.height;
    streamInfo.channels = frame.channels;
    streamInfo.frameRateNumerator = config_.frameRateNumerator;
    streamInfo.frameRateDenominator = config_.frameRateDenominator;

    if (!buildPipeline(streamInfo, error))
        return false;

    streamInfo_ = streamInfo;
    return true;
}

bool FrameBridgeShmPublisher::buildPipeline(const StreamInfo& streamInfo, std::string& error)
{
    namespace fs = std::filesystem;

    if (!streamInfo.socketPath.empty())
    {
        std::error_code ec;
        fs::remove(streamInfo.socketPath, ec);
    }

    pipeline_ = gst_pipeline_new("framebridge-shm-pipeline");
    auto* appsrc = gst_element_factory_make("appsrc", "framebridge-source");
    auto* queue = gst_element_factory_make("queue", "framebridge-queue");
    auto* shmsink = gst_element_factory_make("shmsink", "framebridge-shm-sink");

    if (pipeline_ == nullptr || appsrc == nullptr || queue == nullptr || shmsink == nullptr)
    {
        error = "Failed to create GStreamer appsrc/shmsink pipeline";
        cleanupUnlocked();
        return false;
    }

    appSrc_ = GST_APP_SRC(appsrc);

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format",
                                        G_TYPE_STRING,
                                        streamInfo.gstFormat.c_str(),
                                        "width",
                                        G_TYPE_INT,
                                        static_cast<int>(streamInfo.width),
                                        "height",
                                        G_TYPE_INT,
                                        static_cast<int>(streamInfo.height),
                                        "framerate",
                                        GST_TYPE_FRACTION,
                                        static_cast<int>(streamInfo.frameRateNumerator),
                                        static_cast<int>(streamInfo.frameRateDenominator),
                                        nullptr);

    g_object_set(G_OBJECT(appsrc),
                 "caps",
                 caps,
                 "is-live",
                 TRUE,
                 "block",
                 FALSE,
                 "format",
                 GST_FORMAT_TIME,
                 "do-timestamp",
                 FALSE,
                 "max-buffers",
                 static_cast<guint64>(1),
                 "max-bytes",
                 static_cast<guint64>(0),
                 "max-time",
                 static_cast<guint64>(0),
                 nullptr);
    setObjectPropertyIfAvailable(G_OBJECT(appsrc),
                                 "leaky-type",
                                 static_cast<gint>(GST_APP_LEAKY_TYPE_DOWNSTREAM));
    gst_caps_unref(caps);

    g_object_set(G_OBJECT(queue),
                 "max-size-buffers",
                 1,
                 "max-size-bytes",
                 0,
                 "max-size-time",
                 static_cast<guint64>(0),
                 "leaky",
                 2,
                 nullptr);

    g_object_set(G_OBJECT(shmsink),
                 "socket-path",
                 streamInfo.socketPath.c_str(),
                 "wait-for-connection",
                 FALSE,
                 "sync",
                 FALSE,
                 "async",
                 FALSE,
                 "enable-last-sample",
                 FALSE,
                 "buffer-time",
                 static_cast<gint64>(gst_util_uint64_scale(
                     GST_SECOND,
                     static_cast<guint64>(streamInfo.frameRateDenominator),
                     static_cast<guint64>(streamInfo.frameRateNumerator))),
                 "processing-deadline",
                 static_cast<guint64>(0),
                 "shm-size",
                 config_.shmSizeBytes,
                 nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), appsrc, queue, shmsink, nullptr);
    if (!gst_element_link_many(appsrc, queue, shmsink, nullptr))
    {
        error = "Failed to link GStreamer appsrc/shmsink pipeline";
        cleanupUnlocked();
        return false;
    }

    const auto stateChange = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (stateChange == GST_STATE_CHANGE_FAILURE)
    {
        error = "Failed to set GStreamer appsrc/shmsink pipeline to PLAYING";
        cleanupUnlocked();
        return false;
    }

    return true;
}

std::optional<std::string> FrameBridgeShmPublisher::gstFormatForFrame(
    const streaming::VideoFrameHandle& frame)
{
    switch (frame.format)
    {
    case streaming::PixelFormat::RGB888:
        return std::string("RGB");
    case streaming::PixelFormat::RGBA8888:
        return std::string("RGBA");
    default:
        return std::nullopt;
    }
}

void FrameBridgeShmPublisher::cleanupUnlocked()
{
    if (appSrc_ != nullptr)
    {
        gst_app_src_end_of_stream(appSrc_);
        appSrc_ = nullptr;
    }

    if (pipeline_ != nullptr)
    {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    if (!config_.socketPath.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(config_.socketPath, ignored);
    }

    started_ = false;
    streamInfo_.reset();
    firstTimestampNs_ = 0;
    lastTimestampNs_ = 0;
    lastBufferPtsNs_ = 0;
    haveLastBufferPts_ = false;
}
