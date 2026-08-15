#include "streaming/RecordingConfig.h"
#include "streaming/RecordingDiagnostics.h"

#include <cassert>

namespace
{

void testDefaultConfigIsNotValidUntilOutputAndSizeAreSet()
{
    streaming::RecordingConfig config;

    assert(!streaming::isValid(config));
}

void testValidConfig()
{
    streaming::RecordingConfig config;
    config.outputPath = "recording.mp4";
    config.width = 1920;
    config.height = 1080;
    config.fps = 30.0;
    config.bitrateKbps = 12000;
    config.queueDepth = 8;

    assert(streaming::isValid(config));
}

void testDiagnosticsStartInactive()
{
    streaming::RecordingDiagnostics diagnostics;

    assert(!diagnostics.active);
    assert(diagnostics.selectedPath == streaming::RecordingPath::None);
    assert(diagnostics.receivedFrames == 0);
    assert(diagnostics.writtenFrames == 0);
    assert(diagnostics.droppedFrames == 0);
    assert(!diagnostics.finalized);
    assert(diagnostics.lastError.empty());
}

} // namespace

int main()
{
    testDefaultConfigIsNotValidUntilOutputAndSizeAreSet();
    testValidConfig();
    testDiagnosticsStartInactive();
}
