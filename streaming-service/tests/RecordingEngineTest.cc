#include "streaming/RecordingEngine.h"

#include <cassert>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

namespace
{

class FakeRecordingWriter final : public streaming::RecordingWriter
{
public:
    bool start(const streaming::RecordingConfig&, std::string*) override
    {
        started = true;
        return true;
    }

    bool write(const streaming::RecordingFrame&, std::string*) override
    {
        ++writtenFrames;
        return true;
    }

    bool stop(std::string*) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        stopped = true;
        return true;
    }

    streaming::RecordingPath selectedPath() const override
    {
        return streaming::RecordingPath::Cpu;
    }

    bool started{false};
    bool stopped{false};
    uint64_t writtenFrames{0};
};

streaming::RecordingConfig makeConfig()
{
    streaming::RecordingConfig config;
    config.outputPath = "recording.mp4";
    config.width = 640;
    config.height = 480;
    config.fps = 30.0;
    config.queueDepth = 4;
    return config;
}

streaming::RecordingFrame makeFrame(uint64_t frameNumber)
{
    streaming::RecordingFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.stride = 640 * 4;
    frame.channels = 4;
    frame.byteCount = frame.stride * frame.height;
    frame.format = streaming::PixelFormat::RGBA8888;
    frame.dataType = streaming::FrameDataType::UInt8;
    frame.memoryLocation = streaming::MemoryLocation::Host;
    frame.hostData.resize(frame.stride * frame.height);
    frame.frameNumber = frameNumber;
    return frame;
}

void testInvalidConfigDoesNotStart()
{
    auto writer = std::make_unique<FakeRecordingWriter>();
    streaming::RecordingEngine engine(std::move(writer));

    assert(!engine.start(streaming::RecordingConfig{}));

    const auto diagnostics = engine.diagnostics();
    assert(!diagnostics.active);
    assert(!diagnostics.lastError.empty());
}

void testSubmitReturnsFalseWhenInactive()
{
    auto writer = std::make_unique<FakeRecordingWriter>();
    streaming::RecordingEngine engine(std::move(writer));

    assert(!engine.trySubmit(makeFrame(1)));
}

void testStartSubmitStopWritesAndFinalizes()
{
    auto writer = std::make_unique<FakeRecordingWriter>();
    auto* writerPtr = writer.get();
    streaming::RecordingEngine engine(std::move(writer));

    assert(engine.start(makeConfig()));
    assert(engine.isActive());
    assert(writerPtr->started);

    assert(engine.trySubmit(makeFrame(1)));
    assert(engine.trySubmit(makeFrame(2)));

    engine.stop();
    assert(!engine.isActive());

    const auto diagnostics = engine.diagnostics();
    assert(!diagnostics.active);
    assert(diagnostics.selectedPath == streaming::RecordingPath::Cpu);
    assert(diagnostics.receivedFrames == 2);
    assert(diagnostics.writtenFrames == 2);
    assert(diagnostics.droppedFrames == 0);
    assert(diagnostics.finalized);
    assert(diagnostics.finalizationDurationNs >= diagnostics.writerStopDurationNs);
    assert(diagnostics.writerStopDurationNs >= 1000000);
    assert(writerPtr->stopped);
    assert(writerPtr->writtenFrames == 2);
    engine.stop();
    assert(engine.diagnostics().finalizationDurationNs == diagnostics.finalizationDurationNs);
    assert(engine.start(makeConfig()));
    assert(engine.diagnostics().finalizationDurationNs == 0);
    assert(engine.diagnostics().writerStopDurationNs == 0);
    engine.stop();
}

} // namespace

int main()
{
    testInvalidConfigDoesNotStart();
    testSubmitReturnsFalseWhenInactive();
    testStartSubmitStopWritesAndFinalizes();
}
