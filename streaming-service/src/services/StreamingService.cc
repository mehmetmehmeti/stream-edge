#include "services/StreamingService.h"
#include "services/CloudPublisher.h"
#include "streaming/HoloscanVideoPipeline.h"
#include "streaming/RecordingConfig.h"
#include "streaming/RecordingDiagnostics.h"
#include "streaming/StreamingSourceConfigLoader.h"
#include "streaming/StreamingSourceHealth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace
{
std::string utcIsoString(const std::chrono::system_clock::time_point timePoint)
{
    const auto time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm tm{};
    gmtime_r(&time, &tm);

    std::ostringstream value;
    value << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return value.str();
}

std::optional<std::string> envString(const char* name)
{
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::nullopt;

    return std::string(value);
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
    auto value = envString("STREAMING_RECORD_MODE");
    if (!value)
        return streaming::RecordingMode::Cpu;

    std::transform(value->begin(), value->end(), value->begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    // The Holoscan recording branch currently emits RGB tensors. The CPU
    // writer converts them to RGBA before encoding; the GPU writer requires
    // RGBA CUDA memory directly.
    if (*value == "auto")
        return streaming::RecordingMode::Cpu;
    if (*value == "gpu")
        return streaming::RecordingMode::Gpu;
    if (*value == "cpu")
        return streaming::RecordingMode::Cpu;

    throw std::runtime_error(
        "Invalid STREAMING_RECORD_MODE: " + *value + " (expected auto, gpu, or cpu)");
}

uint32_t configuredWidth(const StreamingSourceConfig& sourceConfig)
{
    if (auto width = envUInt32("STREAMING_RECORD_WIDTH"))
        return *width;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.width)
        return *sourceConfig.camera.width;

    throw std::runtime_error(
        "Set STREAMING_RECORD_WIDTH or STREAMING_CAMERA_WIDTH before recording");
}

uint32_t configuredHeight(const StreamingSourceConfig& sourceConfig)
{
    if (auto height = envUInt32("STREAMING_RECORD_HEIGHT"))
        return *height;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.height)
        return *sourceConfig.camera.height;

    throw std::runtime_error(
        "Set STREAMING_RECORD_HEIGHT or STREAMING_CAMERA_HEIGHT before recording");
}

double configuredFps(const StreamingSourceConfig& sourceConfig)
{
    if (auto fps = envPositiveDouble("STREAMING_RECORD_FPS"))
        return *fps;

    if (sourceConfig.type == StreamingSourceType::Camera && sourceConfig.camera.frameRate)
        return *sourceConfig.camera.frameRate;

    return 30.0;
}

std::filesystem::path recordingOutputPath(const std::string& caseId)
{
    if (auto output = envString("STREAMING_RECORD_OUTPUT"))
    {
        std::filesystem::path configured(*output);
        if (configured.has_extension())
            return configured;

        std::filesystem::create_directories(configured);
        auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&time, &tm);

        std::ostringstream fileName;
        fileName << (caseId.empty() ? "recording" : caseId) << '-'
                 << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".mp4";
        return configured / fileName.str();
    }

    std::filesystem::path outputDir = std::filesystem::current_path() / "recordings";
    std::filesystem::create_directories(outputDir);
    auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream fileName;
    fileName << (caseId.empty() ? "recording" : caseId) << '-'
             << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".mp4";
    return outputDir / fileName.str();
}

streaming::RecordingConfig buildRecordingConfig(const StreamingSourceConfig& sourceConfig,
                                                const std::string& caseId)
{
    streaming::RecordingConfig config;
    config.outputPath = recordingOutputPath(caseId);
    config.width = configuredWidth(sourceConfig);
    config.height = configuredHeight(sourceConfig);
    config.fps = configuredFps(sourceConfig);
    config.mode = envRecordingMode();
    if (auto bitrate = envUInt32("STREAMING_RECORD_BITRATE_KBPS"))
        config.bitrateKbps = *bitrate;
    return config;
}
} // namespace

StreamingService& StreamingService::instance()
{
    static StreamingService service;
    return service;
}

StreamingService::~StreamingService()
{
    (void)stopStream();
}

bool StreamingService::isHoloscanConfigured() const
{
    return HoloscanVideoPipeline::isConfigAvailable(HoloscanVideoPipeline::defaultConfigPath());
}

bool StreamingService::isHoloscanRequiredForCurrentSource(std::string* errorMessage) const
{
    try
    {
        const auto sourceConfig = loadStreamingSourceConfig();
        return sourceConfig.type != StreamingSourceType::Mp4File;
    }
    catch (const std::exception& e)
    {
        if (errorMessage)
            *errorMessage = e.what();
        return true;
    }
}

bool StreamingService::isStreamingSourceConfigured(std::string *errorMessage) const
{
    try
    {
        (void)loadStreamingSourceConfig();
        return true;
    }
    catch (const std::exception &e)
    {
        if (errorMessage)
            *errorMessage = e.what();
        return false;
    }
}

bool StreamingService::hasStreamingSourceSignal(std::string* errorMessage) const
{
    return streaming::isStreamingSourceSignalAvailable(errorMessage);
}

bool StreamingService::startStream(std::string* message)
{
    return startStream({}, message);
}

bool StreamingService::startStream(const std::string& streamNameOverride,
                                   std::string* message)
{
    std::thread staleThread;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_)
        {
            if (message)
                *message = "Video stream is already running";
            return true;
        }

        if (workerThread_.joinable())
            staleThread = std::move(workerThread_);
    }

    if (staleThread.joinable())
        staleThread.join();

    StreamingSourceConfig sourceConfig;
    try
    {
        sourceConfig = loadStreamingSourceConfig();
    }
    catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = e.what();
        if (message)
            *message = lastError_;
        return false;
    }

    std::string sourceSignalError;
    if (!streaming::isStreamingSourceSignalAvailable(&sourceSignalError))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = sourceSignalError.empty()
                         ? "Streaming source signal is unavailable"
                         : sourceSignalError;
        if (message)
            *message = lastError_;
        return false;
    }

    if (sourceConfig.type == StreamingSourceType::Mp4File)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastSourceConfig_ = sourceConfig;
            running_ = true;
            lastError_.clear();
        }

        std::string publisherMessage;
        if (!CloudPublisher::instance().startFilePublisher(
                sourceConfig.mp4.path, streamNameOverride, &publisherMessage))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            lastError_ = publisherMessage.empty() ? "MP4 WHIP publisher failed to start"
                                                  : publisherMessage;
            if (message)
                *message = lastError_;
            return false;
        }

        if (message)
            *message = publisherMessage.empty()
                           ? "MP4 video stream started"
                           : "MP4 video stream started; " + publisherMessage;
        return true;
    }

    const auto configPath = HoloscanVideoPipeline::defaultConfigPath();
    if (!HoloscanVideoPipeline::isConfigAvailable(configPath))
    {
        const auto error = "Holoscan config file not found: " + configPath.string();
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = error;
        if (message)
            *message = error;
        return false;
    }

    auto pipeline = std::make_shared<HoloscanVideoPipeline>();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_ = pipeline;
        lastSourceConfig_ = sourceConfig;
        running_ = true;
        lastError_.clear();
        workerThread_ = std::thread([this, pipeline, sourceConfig, configPath]() {
            try
            {
                if (sourceConfig.type == StreamingSourceType::Camera)
                    std::cout << "Starting camera Holoscan video stream from "
                              << sourceConfig.camera.device.string() << std::endl;
                else
                    std::cout << "Starting file Holoscan video stream from "
                              << sourceConfig.file.directory.string() << std::endl;

                pipeline->run(sourceConfig, configPath);
            }
            catch (const std::exception& e)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastError_ = e.what();
            }

            std::string publisherMessage;
            (void)CloudPublisher::instance().stop(&publisherMessage);

            std::lock_guard<std::mutex> lock(mutex_);
            if (pipeline_ == pipeline)
                pipeline_.reset();
            running_ = false;
        });
    }

    std::string publisherMessage;
    if (!CloudPublisher::instance().start(&publisherMessage, streamNameOverride))
    {
        (void)stopStream();
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = publisherMessage.empty() ? "Cloud publisher failed to start"
                                              : publisherMessage;
        if (message)
            *message = lastError_;
        return false;
    }

    std::string stoppedBeforePublisherError;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || pipeline_ != pipeline)
        {
            stoppedBeforePublisherError =
                lastError_.empty()
                    ? std::string("Video pipeline stopped before cloud publisher started")
                    : lastError_;
        }
    }

    if (!stoppedBeforePublisherError.empty())
    {
        std::string stopMessage;
        (void)CloudPublisher::instance().stop(&stopMessage);

        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = stoppedBeforePublisherError;
        if (message)
            *message = stoppedBeforePublisherError;
        return false;
    }

    if (message)
        *message = publisherMessage.empty()
                       ? "Video stream started"
                       : "Video stream started; " + publisherMessage;
    return true;
}

bool StreamingService::stopStream(std::string* message)
{
    std::shared_ptr<HoloscanVideoPipeline> pipeline;
    std::thread runningThread;
    bool wasAlreadyStopped = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !workerThread_.joinable())
        {
            wasAlreadyStopped = true;
        }
        else
        {
            pipeline = pipeline_;
            if (workerThread_.joinable())
                runningThread = std::move(workerThread_);
        }
    }

    std::string publisherMessage;
    (void)CloudPublisher::instance().stop(&publisherMessage);

    if (wasAlreadyStopped)
    {
        if (message)
            *message = publisherMessage.empty()
                           ? "Video stream is already stopped"
                           : "Video stream is already stopped; " + publisherMessage;
        return true;
    }

    if (pipeline)
        pipeline->stop();

    if (runningThread.joinable())
        runningThread.join();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        pipeline_.reset();
    }

    if (message)
        *message = publisherMessage.empty()
                       ? "Video stream stopped"
                       : "Video stream stopped; " + publisherMessage;
    return true;
}

bool StreamingService::isStreaming() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

std::string StreamingService::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

bool StreamingService::startRecording(const std::string& caseId, std::string* message)
{
    if (!isStreaming())
    {
        std::string startMessage;
        if (!startStream(&startMessage))
        {
            if (message)
                *message = startMessage.empty() ? "Failed to start stream for recording"
                                                : startMessage;
            return false;
        }
    }

    std::shared_ptr<HoloscanVideoPipeline> pipeline;
    std::optional<StreamingSourceConfig> sourceConfig;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline = pipeline_;
        sourceConfig = lastSourceConfig_;
    }

    if (!pipeline || !sourceConfig.has_value())
    {
        const auto error = "Streaming pipeline is unavailable for recording";
        if (message)
            *message = error;
        return false;
    }

    if (pipeline->recordingDiagnostics().active)
    {
        std::string activeCaseId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeCaseId = currentRecordingCaseId_;
        }
        if (!caseId.empty() && !activeCaseId.empty() && caseId != activeCaseId)
        {
            if (message)
                *message = "Recording is already active for another case";
            return false;
        }

        if (message)
            *message = "Recording already active";
        return true;
    }

    try
    {
        const auto config = buildRecordingConfig(*sourceConfig, caseId);
        if (!pipeline->startRecording(config))
        {
            const auto diagnostics = pipeline->recordingDiagnostics();
            const auto error =
                diagnostics.lastError.empty() ? "Failed to start recording" : diagnostics.lastError;
            if (message)
                *message = error;
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentRecordingPath_ = config.outputPath.string();
            currentRecordingCaseId_ = caseId;
            lastRecordingPath_.clear();
            lastRecordingCaseId_.clear();
            currentRecordingStartedAt_ = std::chrono::system_clock::now();
            lastRecordingStartedAt_.reset();
            lastRecordingEndedAt_.reset();
        }

        if (message)
            *message = "Recording started";
        return true;
    }
    catch (const std::exception& e)
    {
        if (message)
            *message = e.what();
        return false;
    }
}

bool StreamingService::stopRecording(std::string* message,
                                     const std::string& caseId)
{
    std::shared_ptr<HoloscanVideoPipeline> pipeline;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline = pipeline_;
    }

    if (!pipeline)
    {
        if (message)
            *message = "Recording is already stopped";
        return true;
    }

    if (!pipeline->recordingDiagnostics().active)
    {
        std::string previousCaseId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousCaseId = lastRecordingCaseId_;
        }
        if (!caseId.empty() && !previousCaseId.empty() && caseId != previousCaseId)
        {
            if (message)
                *message = "No recording is active for this case";
            return false;
        }

        if (message)
            *message = "Recording is already stopped";
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!caseId.empty() && !currentRecordingCaseId_.empty() &&
            caseId != currentRecordingCaseId_)
        {
            if (message)
                *message = "Active recording belongs to another case";
            return false;
        }
    }

    pipeline->stopRecording();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastRecordingPath_ = currentRecordingPath_;
        lastRecordingCaseId_ = currentRecordingCaseId_;
        lastRecordingStartedAt_ = currentRecordingStartedAt_;
        lastRecordingEndedAt_ = std::chrono::system_clock::now();
        currentRecordingPath_.clear();
        currentRecordingCaseId_.clear();
        currentRecordingStartedAt_.reset();
    }
    if (message)
        *message = "Recording stopped";
    return true;
}

bool StreamingService::isRecording() const
{
    std::shared_ptr<HoloscanVideoPipeline> pipeline;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline = pipeline_;
    }
    if (!pipeline)
        return false;

    return pipeline->recordingDiagnostics().active;
}

Json::Value StreamingService::recordingStatus() const
{
    Json::Value json(Json::objectValue);
    std::shared_ptr<HoloscanVideoPipeline> pipeline;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline = pipeline_;
    }

    const auto diagnostics =
        pipeline ? pipeline->recordingDiagnostics() : streaming::RecordingDiagnostics{};
    json["active"] = diagnostics.active;
    json["received_frames"] = Json::UInt64(diagnostics.receivedFrames);
    json["written_frames"] = Json::UInt64(diagnostics.writtenFrames);
    json["dropped_frames"] = Json::UInt64(diagnostics.droppedFrames);
    json["finalized"] = diagnostics.finalized;
    switch (diagnostics.selectedPath)
    {
    case streaming::RecordingPath::Gpu:
        json["path"] = "gpu";
        break;
    case streaming::RecordingPath::Cpu:
        json["path"] = "cpu";
        break;
    case streaming::RecordingPath::None:
    default:
        json["path"] = "none";
        break;
    }
    if (!diagnostics.lastError.empty())
        json["last_error"] = diagnostics.lastError;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& outputPath = currentRecordingPath_.empty()
                                     ? lastRecordingPath_
                                     : currentRecordingPath_;
        const auto& caseId = currentRecordingCaseId_.empty()
                                 ? lastRecordingCaseId_
                                 : currentRecordingCaseId_;
        const auto& startedAt = currentRecordingStartedAt_.has_value()
                                    ? currentRecordingStartedAt_
                                    : lastRecordingStartedAt_;
        if (!outputPath.empty())
            json["output_path"] = outputPath;
        if (!caseId.empty())
            json["case_id"] = caseId;
        if (startedAt.has_value())
            json["recorded_at"] = utcIsoString(*startedAt);
        if (!currentRecordingStartedAt_.has_value() && lastRecordingEndedAt_.has_value())
        {
            json["ended_at"] = utcIsoString(*lastRecordingEndedAt_);
            if (startedAt.has_value())
            {
                const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    *lastRecordingEndedAt_ - *startedAt);
                json["duration"] = Json::Int64(std::max<std::int64_t>(0, duration.count()));
            }
        }
    }
    return json;
}

Json::Value StreamingService::publisherStatus() const
{
    return CloudPublisher::instance().statusJson();
}
