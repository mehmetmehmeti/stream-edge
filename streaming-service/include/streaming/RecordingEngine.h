#pragma once

#include "streaming/RecordingConfig.h"
#include "streaming/RecordingDiagnostics.h"
#include "streaming/RecordingFrame.h"
#include "streaming/RecordingWriter.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace streaming
{

class RecordingEngine
{
public:
    explicit RecordingEngine(std::unique_ptr<RecordingWriter> writer);
    ~RecordingEngine();

    RecordingEngine(const RecordingEngine&) = delete;
    RecordingEngine& operator=(const RecordingEngine&) = delete;

    bool start(const RecordingConfig& config);
    void stop();
    bool trySubmit(RecordingFrame frame);

    bool isActive() const;
    RecordingDiagnostics diagnostics() const;

private:
    void writerLoop();
    void setLastError(const std::string& error);

    mutable std::mutex mutex_;
    std::condition_variable frameAvailable_;
    std::unique_ptr<RecordingWriter> writer_;
    std::deque<RecordingFrame> pendingFrames_;
    std::thread writerThread_;
    RecordingConfig config_;
    RecordingDiagnostics diagnostics_;
    bool stopRequested_{false};
};

} // namespace streaming
