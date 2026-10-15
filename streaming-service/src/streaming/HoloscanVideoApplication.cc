#include "streaming/HoloscanVideoApplication.h"
#include "streaming/QtOutputBridgeOp.h"
#include "streaming/RecordingOp.h"

#include <cstdint>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <holoscan/operators/format_converter/format_converter.hpp>
#include <holoscan/operators/holoviz/holoviz.hpp>
#include <holoscan/operators/v4l2_video_capture/v4l2_video_capture.hpp>
#include <holoscan/operators/video_stream_replayer/video_stream_replayer.hpp>
#include <memory>
#include <linux/videodev2.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
void applyCameraFrameRatePolicy(const CameraVideoSourceConfig& config)
{
    if (!config.dynamicFrameRate.has_value())
        return;

    const int fd = ::open(config.device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("Cannot open camera controls: " + std::string(std::strerror(errno)));
    struct Handle
    {
        int fd;
        ~Handle() { ::close(fd); }
    } handle{fd};

    v4l2_queryctrl query{};
    query.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    if (::ioctl(fd, VIDIOC_QUERYCTRL, &query) < 0)
    {
        if (errno != EINVAL && errno != ENOTTY)
            throw std::runtime_error("Cannot query camera frame-rate control: " + std::string(std::strerror(errno)));
        HOLOSCAN_LOG_INFO("Camera {} does not expose dynamic frame rate; requested policy {} is unsupported",
                          config.device.string(), *config.dynamicFrameRate ? 1 : 0);
        return;
    }
    if (query.flags & V4L2_CTRL_FLAG_DISABLED)
    {
        HOLOSCAN_LOG_INFO("Camera {} dynamic-frame-rate control is unavailable",
                          config.device.string());
        return;
    }
    v4l2_control control{};
    control.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    control.value = *config.dynamicFrameRate ? 1 : 0;
    const int requested = control.value;
    if (::ioctl(fd, VIDIOC_S_CTRL, &control) < 0 || ::ioctl(fd, VIDIOC_G_CTRL, &control) < 0)
        throw std::runtime_error("Cannot apply/verify camera frame-rate control: " + std::string(std::strerror(errno)));
    if (control.value != requested)
        throw std::runtime_error("Camera dynamic-frame-rate control did not retain the requested value");
    HOLOSCAN_LOG_INFO("Camera {} dynamic frame rate verified: {}", config.device.string(), control.value);
}

class ConfiguredCameraOp : public holoscan::ops::V4L2VideoCaptureOp
{
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS_SUPER(ConfiguredCameraOp, holoscan::ops::V4L2VideoCaptureOp)
    ConfiguredCameraOp() = default;

    void configureFrameRatePolicy(const CameraVideoSourceConfig& config) { config_ = config; }

    void start() override
    {
        holoscan::ops::V4L2VideoCaptureOp::start();
        // SDK startup may reset exposure controls, so enforce the policy afterward.
        try { applyCameraFrameRatePolicy(config_); }
        catch (...) { holoscan::ops::V4L2VideoCaptureOp::stop(); throw; }
    }

private:
    CameraVideoSourceConfig config_;
};

bool previewEnabled()
{
    const char* raw = std::getenv("STREAMING_ENABLE_PREVIEW");
    if (raw == nullptr || *raw == '\0')
        return true;

    const std::string value(raw);
    return !(value == "0" || value == "false" || value == "FALSE" || value == "no");
}

bool stopStreamOnPreviewClose()
{
    const char* raw = std::getenv("STREAMING_STOP_ON_PREVIEW_CLOSE");
    if (raw == nullptr || *raw == '\0')
        return false;

    const std::string value(raw);
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

class CameraFramePassthroughOp : public holoscan::Operator
{
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS(CameraFramePassthroughOp)
    CameraFramePassthroughOp() = default;

    void setup(holoscan::OperatorSpec& spec) override
    {
        spec.input<nvidia::gxf::Entity>("input");
        spec.output<nvidia::gxf::Entity>("output");
    }

    void compute(holoscan::InputContext& opInput,
                 holoscan::OutputContext& opOutput,
                 holoscan::ExecutionContext& context) override
    {
        (void)context;

        auto maybeEntity = opInput.receive<nvidia::gxf::Entity>("input");
        if (!maybeEntity)
        {
            HOLOSCAN_LOG_ERROR("Failed to receive camera frame: {}",
                               maybeEntity.error().what());
            return;
        }

        opOutput.emit(maybeEntity.value(), "output");
    }
};

class FrameDrainOp : public holoscan::Operator
{
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS(FrameDrainOp)
    FrameDrainOp() = default;

    void setup(holoscan::OperatorSpec& spec) override
    {
        spec.input<nvidia::gxf::Entity>("input");
    }

    void compute(holoscan::InputContext& opInput,
                 holoscan::OutputContext& opOutput,
                 holoscan::ExecutionContext& context) override
    {
        (void)opOutput;
        (void)context;

        auto maybeEntity = opInput.receive<nvidia::gxf::Entity>("input");
        if (!maybeEntity)
        {
            HOLOSCAN_LOG_ERROR("Failed to receive bridged frame: {}",
                               maybeEntity.error().what());
        }
    }
};
} // namespace

HoloscanVideoApplication::HoloscanVideoApplication(StreamingSourceConfig sourceConfig)
    : sourceConfig_(std::move(sourceConfig))
{
}

void HoloscanVideoApplication::onWindowClosed()
{
    if (!stopStreamOnPreviewClose())
    {
        HOLOSCAN_LOG_INFO(
            "Preview window close callback received; keeping stream alive because "
            "STREAMING_STOP_ON_PREVIEW_CLOSE is disabled");
        return;
    }

    stop_execution();
}

void HoloscanVideoApplication::compose()
{
    switch (sourceConfig_.type)
    {
    case StreamingSourceType::File:
        composeFileSource();
        break;
    case StreamingSourceType::Camera:
        composeCameraSource();
        break;
    case StreamingSourceType::Mp4File:
        throw std::runtime_error("MP4 files are published directly through WHIP, not Holoscan");
    }
}

void HoloscanVideoApplication::composeFileSource()
{
    using namespace holoscan;

    ArgList args;
    args.add(Arg("directory", sourceConfig_.file.directory.string()));
    args.add(Arg("basename", sourceConfig_.file.basename));
    args.add(Arg("allocator",
                 make_resource<RMMAllocator>("rmm_allocator",
                                             from_config("rmm_allocator"))));

    auto replayer = make_operator<ops::VideoStreamReplayerOp>(
        "replayer", from_config("replayer"), args);
    auto recordingOp = make_operator<RecordingOp>("recording_op");

    add_flow(replayer, recordingOp, {{"output", "input"}});
    if (previewEnabled())
    {
        auto visualizer = make_operator<ops::HolovizOp>(
            "holoviz", from_config("holoviz"),
            Arg("window_close_callback", [this]() { onWindowClosed(); }));
        add_flow(replayer, visualizer, {{"output", "receivers"}});
    }
}

void HoloscanVideoApplication::composeCameraSource()
{
    using namespace holoscan;

    const bool enablePreview = previewEnabled();
    auto cudaStreamPool = make_resource<CudaStreamPool>("camera_cuda_stream", 0, 0, 0, 1, 1);

    ArgList cameraArgs;
    addCameraArgs(cameraArgs);

    auto camera = make_operator<ConfiguredCameraOp>(
        "camera", Arg("pass_through", true), from_config("camera"), cameraArgs);
    camera->configureFrameRatePolicy(sourceConfig_.camera);

    auto pool = make_resource<RMMAllocator>("rmm_allocator", from_config("rmm_allocator"));
    auto yuyvToRgbConverter = make_operator<ops::FormatConverterOp>(
        "format_converter",
        Arg("in_dtype") = std::string("yuyv"),
        Arg("out_dtype") = std::string("rgb888"),
        Arg("pool") = pool,
        from_config("format_converter"));
    auto rgbToRgbaConverter = make_operator<ops::FormatConverterOp>(
        "rgb_to_rgba_converter",
        Arg("in_dtype") = std::string("rgb888"),
        Arg("out_dtype") = std::string("rgba8888"),
        Arg("pool") = pool);
    auto qtOutputBridge = make_operator<QtOutputBridgeOp>("qt_output_bridge");
    auto recordingOp = make_operator<RecordingOp>("recording_op");

    add_flow(camera, yuyvToRgbConverter, {{"signal", "source_video"}});
    add_flow(yuyvToRgbConverter, rgbToRgbaConverter, {{"tensor", "source_video"}});
    add_flow(rgbToRgbaConverter, qtOutputBridge, {{"tensor", "input"}});
    add_flow(rgbToRgbaConverter, recordingOp, {{"tensor", "input"}});

    if (enablePreview)
    {
        auto passthrough = make_operator<CameraFramePassthroughOp>("camera_passthrough");

        auto visualizerArgs = from_config("holoviz");
        addVisualizerResolutionArgs(visualizerArgs);
        auto visualizer = make_operator<ops::HolovizOp>(
            "holoviz",
            visualizerArgs,
            Arg("window_close_callback", [this]() { onWindowClosed(); }),
            Arg("cuda_stream_pool", cudaStreamPool));

        add_flow(qtOutputBridge, passthrough, {{"output", "input"}});
        add_flow(camera, passthrough, {{"signal", "input"}});
        add_flow(passthrough, visualizer, {{"output", "receivers"}});

        set_dynamic_flows(camera,
                          [yuyvToRgbConverter,
                           passthrough](const std::shared_ptr<Operator>& op) {
                              auto pixelFormat =
                                  op->metadata()->get<std::string>("V4L2_pixel_format", "");

                              if (pixelFormat.find("YUYV") != std::string::npos ||
                                  pixelFormat.find("yuyv") != std::string::npos)
                              {
                                  op->add_dynamic_flow(yuyvToRgbConverter);
                                  return;
                              }

                              op->add_dynamic_flow(passthrough);
                          });
    }
    else
    {
        auto frameDrain = make_operator<FrameDrainOp>("frame_drain");
        add_flow(qtOutputBridge, frameDrain, {{"output", "input"}});

        HOLOSCAN_LOG_INFO(
            "Holoviz preview disabled via STREAMING_ENABLE_PREVIEW; streaming will continue headless");
        set_dynamic_flows(camera,
                          [yuyvToRgbConverter](const std::shared_ptr<Operator>& op) {
                              auto pixelFormat =
                                  op->metadata()->get<std::string>("V4L2_pixel_format", "");

                              if (pixelFormat.find("YUYV") != std::string::npos ||
                                  pixelFormat.find("yuyv") != std::string::npos)
                                  op->add_dynamic_flow(yuyvToRgbConverter);
                          });
    }
}

void HoloscanVideoApplication::addCameraArgs(holoscan::ArgList& args) const
{
    args.add(holoscan::Arg("device", sourceConfig_.camera.device.string()));

    if (sourceConfig_.camera.width)
        args.add(holoscan::Arg("width", static_cast<uint32_t>(*sourceConfig_.camera.width)));

    if (sourceConfig_.camera.height)
        args.add(holoscan::Arg("height", static_cast<uint32_t>(*sourceConfig_.camera.height)));

    if (sourceConfig_.camera.frameRate)
        args.add(holoscan::Arg("frame_rate", *sourceConfig_.camera.frameRate));

    if (sourceConfig_.camera.pixelFormat)
        args.add(holoscan::Arg("pixel_format", *sourceConfig_.camera.pixelFormat));

    if (sourceConfig_.camera.exposureTime)
        args.add(holoscan::Arg("exposure_time",
                               static_cast<uint32_t>(*sourceConfig_.camera.exposureTime)));

    if (sourceConfig_.camera.gain)
        args.add(holoscan::Arg("gain", static_cast<uint32_t>(*sourceConfig_.camera.gain)));
}

void HoloscanVideoApplication::addVisualizerResolutionArgs(holoscan::ArgList& args) const
{
    if (!sourceConfig_.camera.width || !sourceConfig_.camera.height)
        return;

    args.add(holoscan::Arg("width", static_cast<uint32_t>(*sourceConfig_.camera.width)));
    args.add(holoscan::Arg("height", static_cast<uint32_t>(*sourceConfig_.camera.height)));
}
