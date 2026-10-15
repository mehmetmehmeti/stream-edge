#include "streaming/RecordingEngine.h"

#include <utility>
#include <chrono>

namespace streaming
{

RecordingEngine::RecordingEngine(std::unique_ptr<RecordingWriter> writer)
    : writer_(std::move(writer))
{
}

RecordingEngine::~RecordingEngine()
{
    stop();
}

bool RecordingEngine::start(const RecordingConfig& config)
{
    {
        std::lock_guard lock(mutex_);
        if (diagnostics_.active)
        {
            diagnostics_.lastError = "Recording is already active";
            return false;
        }
    }

    if (writerThread_.joinable())
        writerThread_.join();

    if (!isValid(config))
    {
        setLastError("Invalid recording config");
        return false;
    }

    if (!writer_)
    {
        setLastError("Recording writer is not configured");
        return false;
    }

    std::string errorMessage;
    if (!writer_->start(config, &errorMessage))
    {
        setLastError(errorMessage.empty() ? "Failed to start recording writer" : errorMessage);
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        config_ = config;
        diagnostics_ = RecordingDiagnostics{};
        diagnostics_.active = true;
        diagnostics_.selectedPath = writer_->selectedPath();
        pendingFrames_.clear();
        stopRequested_ = false;
    }

    writerThread_ = std::thread([this]() { writerLoop(); });
    return true;
}

void RecordingEngine::stop()
{
    const auto started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        if (!diagnostics_.active && !writerThread_.joinable())
            return;

        diagnostics_.active = false;
        stopRequested_ = true;
    }

    frameAvailable_.notify_all();

    if (writerThread_.joinable())
        writerThread_.join();
    std::lock_guard lock(mutex_);
    diagnostics_.finalizationDurationNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
}

bool RecordingEngine::trySubmit(RecordingFrame frame)
{
    {
        std::lock_guard lock(mutex_);
        if (!diagnostics_.active || stopRequested_)
            return false;

        ++diagnostics_.receivedFrames;

        if (pendingFrames_.size() >= config_.queueDepth)
        {
            ++diagnostics_.droppedFrames;
            return false;
        }

        pendingFrames_.push_back(std::move(frame));
    }

    frameAvailable_.notify_one();
    return true;
}

bool RecordingEngine::isActive() const
{
    std::lock_guard lock(mutex_);
    return diagnostics_.active;
}

RecordingDiagnostics RecordingEngine::diagnostics() const
{
    std::lock_guard lock(mutex_);
    return diagnostics_;
}

void RecordingEngine::writerLoop()
{
    while (true)
    {
        RecordingFrame frame;
        {
            std::unique_lock lock(mutex_);
            frameAvailable_.wait(lock, [this]() {
                return stopRequested_ || !pendingFrames_.empty();
            });

            if (pendingFrames_.empty() && stopRequested_)
                break;

            frame = std::move(pendingFrames_.front());
            pendingFrames_.pop_front();
        }

        std::string errorMessage;
        if (writer_->write(frame, &errorMessage))
        {
            std::lock_guard lock(mutex_);
            ++diagnostics_.writtenFrames;
        }
        else
        {
            std::lock_guard lock(mutex_);
            ++diagnostics_.droppedFrames;
            diagnostics_.lastError =
                errorMessage.empty() ? "Failed to write recording frame" : errorMessage;
        }
    }

    std::string errorMessage;
    const auto stopStarted = std::chrono::steady_clock::now();
    const bool stopped = writer_->stop(&errorMessage);

    std::lock_guard lock(mutex_);
    diagnostics_.finalized = stopped;
    diagnostics_.writerStopDurationNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - stopStarted).count());
    if (!stopped)
        diagnostics_.lastError =
            errorMessage.empty() ? "Failed to finalize recording writer" : errorMessage;
}

void RecordingEngine::setLastError(const std::string& error)
{
    std::lock_guard lock(mutex_);
    diagnostics_.lastError = error;
}

} // namespace streaming
