#pragma once

#include "streaming/VideoFrameHandle.h"

#include <json/json.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

typedef struct _GstElement GstElement;
typedef struct _GstAppSrc GstAppSrc;

class FrameBridgeShmPublisher
{
public:
    struct Config
    {
        std::string socketPath;
        std::uint32_t frameRateNumerator{30};
        std::uint32_t frameRateDenominator{1};
        std::uint32_t shmSizeBytes{16U * 1024U * 1024U};
    };

    struct StreamInfo
    {
        std::string socketPath;
        std::string gstFormat;
        std::uint32_t width{0};
        std::uint32_t height{0};
        std::uint32_t channels{0};
        std::uint32_t frameRateNumerator{30};
        std::uint32_t frameRateDenominator{1};
    };

    FrameBridgeShmPublisher() = default;
    ~FrameBridgeShmPublisher();

    bool start(const Config& config, std::string& error);
    bool pushFrame(const streaming::VideoFrameHandle& frame, std::string& error);
    void stop();

    bool isRunning() const;
    Json::Value statusJson() const;
    std::optional<StreamInfo> streamInfo() const;

private:
    bool ensurePipelineForFrame(const streaming::VideoFrameHandle& frame, std::string& error);
    bool buildPipeline(const StreamInfo& streamInfo, std::string& error);
    static std::optional<std::string> gstFormatForFrame(const streaming::VideoFrameHandle& frame);
    void cleanupUnlocked();

    mutable std::mutex mutex_;
    Config config_{};
    std::optional<StreamInfo> streamInfo_{};
    std::uint64_t framesPushed_{0};
    std::uint64_t bytesPushed_{0};
    std::uint64_t lastFrameNumber_{0};
    std::uint64_t firstTimestampNs_{0};
    std::uint64_t lastTimestampNs_{0};
    std::uint64_t lastBufferPtsNs_{0};
    bool haveLastBufferPts_{false};
    std::string lastError_{};
    bool started_{false};

    GstElement* pipeline_{nullptr};
    GstAppSrc* appSrc_{nullptr};
};
