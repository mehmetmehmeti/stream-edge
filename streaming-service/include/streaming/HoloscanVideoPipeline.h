#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "streaming/RecordingConfig.h"
#include "streaming/RecordingDiagnostics.h"
#include "streaming/StreamingSourceConfig.h"

namespace holoscan
{
class Application;
}

namespace streaming
{
class RecordingEngine;
}

class HoloscanVideoPipeline
{
public:
    HoloscanVideoPipeline();

    static std::filesystem::path defaultConfigPath();
    static bool isConfigAvailable(const std::filesystem::path& configPath);

    void run(const StreamingSourceConfig& sourceConfig,
             const std::filesystem::path& configPath);
    void stop();

    bool startRecording(const streaming::RecordingConfig& config);
    void stopRecording();
    streaming::RecordingDiagnostics recordingDiagnostics() const;

private:
    void stopAutoRecordingTimer();

    mutable std::mutex mutex_;
    std::shared_ptr<holoscan::Application> app_;
    std::shared_ptr<streaming::RecordingEngine> recordingEngine_;
    std::jthread autoRecordingThread_;
    bool running_ = false;
    bool stopRequested_ = false;
};
