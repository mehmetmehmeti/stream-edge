#pragma once

#include <json/json.h>
#include "streaming/StreamingSourceConfig.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class HoloscanVideoPipeline;

class StreamingService {
public:
    static StreamingService& instance();
    ~StreamingService();

    bool isHoloscanConfigured() const;
    bool isStreamingSourceConfigured(std::string* errorMessage = nullptr) const;
    bool hasStreamingSourceSignal(std::string* errorMessage = nullptr) const;
    bool startStream(std::string* message = nullptr);
    bool stopStream(std::string* message = nullptr);
    bool startRecording(const std::string& caseId = {}, std::string* message = nullptr);
    bool stopRecording(std::string* message = nullptr,
                       const std::string& caseId = {});
    bool isStreaming() const;
    bool isRecording() const;
    std::string lastError() const;
    Json::Value recordingStatus() const;
    Json::Value publisherStatus() const;

private:
    StreamingService() = default;
    StreamingService(const StreamingService&) = delete;
    StreamingService& operator=(const StreamingService&) = delete;

    mutable std::mutex mutex_;
    std::shared_ptr<HoloscanVideoPipeline> pipeline_;
    std::thread workerThread_;
    bool running_ = false;
    std::string lastError_;
    std::string currentRecordingPath_;
    std::string lastRecordingPath_;
    std::string currentRecordingCaseId_;
    std::string lastRecordingCaseId_;
    std::optional<std::chrono::system_clock::time_point> currentRecordingStartedAt_;
    std::optional<std::chrono::system_clock::time_point> lastRecordingStartedAt_;
    std::optional<std::chrono::system_clock::time_point> lastRecordingEndedAt_;
    std::optional<StreamingSourceConfig> lastSourceConfig_;
};
