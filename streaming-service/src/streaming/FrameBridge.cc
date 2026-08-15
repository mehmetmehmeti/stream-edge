#include "streaming/FrameBridge.h"

namespace streaming
{

void FrameBridge::publishFrame(const VideoFrameHandle& frame)
{
    std::optional<VideoFrameHandle> previousFrame;

    {
        std::lock_guard lock(mutex_);

        if (latestFrame_ && latestFrame_->frameNumber != lastLegacyAcquiredFrameNumber_)
            ++droppedFrameCount_;

        previousFrame.swap(latestFrame_);
        latestFrame_ = frame;
        ++publishedFrameCount_;
    }

    frameAvailable_.notify_all();
}

bool FrameBridge::acquireLatestFrame(VideoFrameHandle& out)
{
    std::lock_guard lock(mutex_);

    if (!latestFrame_ || latestFrame_->frameNumber == lastLegacyAcquiredFrameNumber_)
        return false;

    out = *latestFrame_;
    lastLegacyAcquiredFrameNumber_ = out.frameNumber;
    ++acquiredFrameCount_;

    return true;
}

bool FrameBridge::acquireFrameAfter(uint64_t lastFrameNumber, VideoFrameHandle& out)
{
    std::lock_guard lock(mutex_);

    if (!latestFrame_ || latestFrame_->frameNumber == lastFrameNumber)
        return false;

    out = *latestFrame_;
    ++acquiredFrameCount_;
    return true;
}

bool FrameBridge::waitForNewFrame(VideoFrameHandle& out, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    if (!frameAvailable_.wait_for(lock, timeout, [this]() {
            return latestFrame_.has_value() &&
                   latestFrame_->frameNumber != lastLegacyWaitFrameNumber_;
        }))
    {
        return false;
    }

    out = *latestFrame_;
    lastLegacyWaitFrameNumber_ = out.frameNumber;
    ++acquiredFrameCount_;
    return true;
}

bool FrameBridge::waitForFrameAfter(uint64_t lastFrameNumber,
                                    VideoFrameHandle& out,
                                    std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    if (!frameAvailable_.wait_for(lock, timeout, [this, lastFrameNumber]() {
            return latestFrame_.has_value() && latestFrame_->frameNumber != lastFrameNumber;
        }))
    {
        return false;
    }

    out = *latestFrame_;
    ++acquiredFrameCount_;
    return true;
}

void FrameBridge::releaseFrame(uint64_t frameNumber)
{
    std::lock_guard lock(mutex_);
    lastReleasedFrameNumber_ = frameNumber;
}

uint64_t FrameBridge::publishedFrameCount() const
{
    std::lock_guard lock(mutex_);
    return publishedFrameCount_;
}

uint64_t FrameBridge::acquiredFrameCount() const
{
    std::lock_guard lock(mutex_);
    return acquiredFrameCount_;
}

uint64_t FrameBridge::droppedFrameCount() const
{
    std::lock_guard lock(mutex_);
    return droppedFrameCount_;
}

std::optional<uint64_t> FrameBridge::lastReleasedFrameNumber() const
{
    std::lock_guard lock(mutex_);
    return lastReleasedFrameNumber_;
}

} // namespace streaming
