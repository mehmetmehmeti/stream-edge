#pragma once

#include "streaming/VideoFrameHandle.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace streaming
{

class FrameBridge
{
public:
    void publishFrame(const VideoFrameHandle& frame);
    bool acquireLatestFrame(VideoFrameHandle& out);
    bool acquireFrameAfter(uint64_t lastFrameNumber, VideoFrameHandle& out);
    bool waitForNewFrame(VideoFrameHandle& out, std::chrono::milliseconds timeout);
    bool waitForFrameAfter(uint64_t lastFrameNumber,
                           VideoFrameHandle& out,
                           std::chrono::milliseconds timeout);
    void releaseFrame(uint64_t frameNumber);

    uint64_t publishedFrameCount() const;
    uint64_t acquiredFrameCount() const;
    uint64_t droppedFrameCount() const;
    std::optional<uint64_t> lastReleasedFrameNumber() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable frameAvailable_;
    std::optional<VideoFrameHandle> latestFrame_;
    uint64_t lastLegacyAcquiredFrameNumber_{0};
    uint64_t lastLegacyWaitFrameNumber_{0};
    uint64_t publishedFrameCount_{0};
    uint64_t acquiredFrameCount_{0};
    uint64_t droppedFrameCount_{0};
    std::optional<uint64_t> lastReleasedFrameNumber_;
};

} // namespace streaming
