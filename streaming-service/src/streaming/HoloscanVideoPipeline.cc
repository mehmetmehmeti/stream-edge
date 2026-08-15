#include "streaming/HoloscanVideoPipeline.h"
#include "streaming/HoloscanVideoApplication.h"
#include "streaming/RecordingOp.h"

#include <holoscan/holoscan.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
constexpr const char* kRecordingOutputEnv = "STREAMING_RECORD_OUTPUT";
constexpr const char* kRecordingOutputDirEnv = "STREAMING_RECORD_OUTPUT_DIR";
constexpr const char* kRecordingSecondsEnv = "STREAMING_RECORD_SECONDS";
constexpr const char* kRecordingModeEnv = "STREAMING_RECORD_MODE";
constexpr const char* kRecordingWidthEnv = "STREAMING_RECORD_WIDTH";
constexpr const char* kRecordingHeightEnv = "STREAMING_RECORD_HEIGHT";
constexpr const char* kRecordingFpsEnv = "STREAMING_RECORD_FPS";
constexpr const char* kRecordingBitrateEnv = "STREAMING_RECORD_BITRATE_KBPS";
// Auto-recording (start a fixed-duration recording as soon as the pipeline runs)
// is opt-in. When the frontend drives recording via its Start/Stop controls
// (embedded pipeline) this must stay off, otherwise the manual start collides
// with the already-active auto recording ("recording already active").
constexpr const char* kAutoRecordEnv = "STREAMING_AUTO_RECORD";
constexpr uint32_t kDefaultRecordingSeconds = 60 * 60;

std::optional<std::string> envString(const char* name)
{
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::nullopt;

    return std::string(value);
}

bool envBool(const char* name)
{
    auto value = envString(name);
    if (!value)
        return false;

    std::string lowered;
    lowered.reserve(value->size());
    for (char ch : *value)
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

std::optional<uint32_t> envUInt32(const char* name)
{
    auto value = envString(name);
    if (!value)
        return std::nullopt;

    std::size_t parsedLength = 0;
    unsigned long parsedValue = 0;
    try
    {
        parsedValue = std::stoul(*value, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(std::string("Invalid positive integer for ") + name +
                                 ": " + *value);
    }

    if (parsedLength != value->size() || parsedValue == 0 ||
        parsedValue > std::numeric_limits<uint32_t>::max())
    {
        throw std::runtime_error(std::string("Invalid positive integer for ") + name +
                                 ": " + *value);
    }

    return static_cast<uint32_t>(parsedValue);
}

std::optional<double> envPositiveDouble(const char* name)
{
    auto value = envString(name);
    if (!value)
        return std::nullopt;

    std::size_t parsedLength = 0;
    double parsedValue = 0.0;
    try
    {
        parsedValue = std::stod(*value, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(std::string("Invalid positive number for ") + name +
                                 ": " + *value);
    }

    if (parsedLength != value->size() || !std::isfinite(parsedValue) || parsedValue <= 0.0)
    {
        throw std::runtime_error(std::string("Invalid positive number for ") + name +
                                 ": " + *value);
    }

    return parsedValue;
}

streaming::RecordingMode envRecordingMode()
{
    auto value = envString(kRecordingModeEnv);
    if (!value)
        return streaming::RecordingMode::Auto;

    std::transform(value->begin(), value->end(), value->begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (*value == "auto")
        return streaming::RecordingMode::Auto;
    if (*value == "gpu")
        return streaming::RecordingMode::Gpu;
    if (*value == "cpu")
        return streaming::RecordingMode::Cpu;

    throw std::runtime_error(
        "Invalid STREAMING_RECORD_MODE: " + *value + " (expected auto, gpu, or cpu)");
}

uint32_t configuredWidth(const StreamingSourceConfig& sourceConfig)
{
    if (auto width = envUInt32(kRecordingWidthEnv))
        return *width;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.width)
        return *sourceConfig.camera.width;

    throw std::runtime_error(
        "Set STREAMING_RECORD_WIDTH or STREAMING_CAMERA_WIDTH before auto recording");
}

uint32_t configuredHeight(const StreamingSourceConfig& sourceConfig)
{
    if (auto height = envUInt32(kRecordingHeightEnv))
        return *height;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.height)
        return *sourceConfig.camera.height;

    throw std::runtime_error(
        "Set STREAMING_RECORD_HEIGHT or STREAMING_CAMERA_HEIGHT before auto recording");
}

double configuredFps(const StreamingSourceConfig& sourceConfig)
{
    if (auto fps = envPositiveDouble(kRecordingFpsEnv))
        return *fps;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.frameRate)
        return *sourceConfig.camera.frameRate;

    return 30.0;
}

std::optional<streaming::RecordingConfig> autoRecordingConfig(
    const StreamingSourceConfig& sourceConfig)
{
    // Off by default; the frontend controls recording via Start/Stop. Opt in with
    // STREAMING_AUTO_RECORD=1 for standalone/headless capture.
    if (!envBool(kAutoRecordEnv))
        return std::nullopt;

    auto outputPath = envString(kRecordingOutputEnv);
    if (!outputPath)
        outputPath = envString(kRecordingOutputDirEnv);
    if (!outputPath)
        return std::nullopt;

    std::filesystem::path resolvedOutput(*outputPath);
    if (!resolvedOutput.has_extension())
    {
        std::filesystem::create_directories(resolvedOutput);

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&time, &tm);

        std::ostringstream fileName;
        fileName << "recording-" << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".mp4";
        resolvedOutput /= fileName.str();
    }

    streaming::RecordingConfig config;
    config.outputPath = std::move(resolvedOutput);
    config.width = configuredWidth(sourceConfig);
    config.height = configuredHeight(sourceConfig);
    config.fps = configuredFps(sourceConfig);
    config.mode = envRecordingMode();

    if (auto bitrateKbps = envUInt32(kRecordingBitrateEnv))
        config.bitrateKbps = *bitrateKbps;

    const auto parentPath = config.outputPath.parent_path();
    if (!parentPath.empty())
        std::filesystem::create_directories(parentPath);

    return config;
}

uint32_t autoRecordingSeconds()
{
    return envUInt32(kRecordingSecondsEnv).value_or(kDefaultRecordingSeconds);
}

const char* recordingPathName(streaming::RecordingPath path)
{
    switch (path)
    {
    case streaming::RecordingPath::Gpu:
        return "gpu";
    case streaming::RecordingPath::Cpu:
        return "cpu";
    case streaming::RecordingPath::None:
        return "none";
    }

    return "unknown";
}
} // namespace

HoloscanVideoPipeline::HoloscanVideoPipeline()
    : recordingEngine_(RecordingOp::sharedRecordingEngine())
{
}

std::filesystem::path HoloscanVideoPipeline::defaultConfigPath()
{
    return std::filesystem::current_path() / "config" / "video_stream.yaml";
}

bool HoloscanVideoPipeline::isConfigAvailable(const std::filesystem::path& configPath)
{
    return std::filesystem::exists(configPath);
}

void HoloscanVideoPipeline::run(const StreamingSourceConfig& sourceConfig,
                                const std::filesystem::path& configPath)
{
    if (!std::filesystem::exists(configPath))
        throw std::runtime_error("Holoscan config file not found: " + configPath.string());

    auto app = holoscan::make_application<HoloscanVideoApplication>(sourceConfig);
    app->config(configPath);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        app_ = app;
        running_ = true;

        // Handle stop requests that can happen before the worker reaches app->run().
        if (stopRequested_)
            app_->stop_execution();
    }

    const auto recordingConfig = autoRecordingConfig(sourceConfig);
    if (recordingConfig)
    {
        const auto duration = std::chrono::seconds(autoRecordingSeconds());
        if (startRecording(*recordingConfig))
        {
            HOLOSCAN_LOG_INFO("Auto recording started: {}",
                              recordingConfig->outputPath.string());
            autoRecordingThread_ = std::jthread([this, duration](std::stop_token stopToken) {
                const auto deadline = std::chrono::steady_clock::now() + duration;
                while (!stopToken.stop_requested() &&
                       std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                if (!stopToken.stop_requested())
                {
                    stopRecording();
                    const auto diagnostics = recordingDiagnostics();
                    HOLOSCAN_LOG_INFO(
                        "Auto recording stopped: path={}, received={}, written={}, dropped={}, finalized={}, lastError={}",
                        recordingPathName(diagnostics.selectedPath),
                        diagnostics.receivedFrames,
                        diagnostics.writtenFrames,
                        diagnostics.droppedFrames,
                        diagnostics.finalized,
                        diagnostics.lastError.empty() ? "none" : diagnostics.lastError);
                }
            });
        }
        else
        {
            const auto diagnostics = recordingDiagnostics();
            HOLOSCAN_LOG_WARN("Auto recording failed to start: {}",
                              diagnostics.lastError);
        }
    }

    try
    {
        app->run();
    }
    catch (...)
    {
        stopAutoRecordingTimer();
        stopRecording();

        std::lock_guard<std::mutex> lock(mutex_);
        app_.reset();
        running_ = false;
        stopRequested_ = false;
        throw;
    }

    stopAutoRecordingTimer();
    stopRecording();

    std::lock_guard<std::mutex> lock(mutex_);
    app_.reset();
    running_ = false;
    stopRequested_ = false;
}

void HoloscanVideoPipeline::stop()
{
    stopAutoRecordingTimer();
    stopRecording();

    std::shared_ptr<holoscan::Application> app;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
        app = app_;
    }

    if (app)
        app->stop_execution();
}

void HoloscanVideoPipeline::stopAutoRecordingTimer()
{
    if (!autoRecordingThread_.joinable())
        return;

    autoRecordingThread_.request_stop();
    autoRecordingThread_.join();
}

bool HoloscanVideoPipeline::startRecording(const streaming::RecordingConfig& config)
{
    auto engine = recordingEngine_;
    if (!engine)
        return false;

    return engine->start(config);
}

void HoloscanVideoPipeline::stopRecording()
{
    auto engine = recordingEngine_;
    if (engine)
        engine->stop();
}

streaming::RecordingDiagnostics HoloscanVideoPipeline::recordingDiagnostics() const
{
    auto engine = recordingEngine_;
    if (!engine)
        return {};

    return engine->diagnostics();
}
