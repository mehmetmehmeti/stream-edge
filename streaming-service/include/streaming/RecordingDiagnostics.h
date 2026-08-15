#pragma once

#include "streaming/RecordingConfig.h"

#include <cstdint>
#include <string>

namespace streaming
{

enum class RecordingPath
{
    None,
    Gpu,
    Cpu
};

struct RecordingDiagnostics
{
    bool active{false};
    RecordingPath selectedPath{RecordingPath::None};
    uint64_t receivedFrames{0};
    uint64_t writtenFrames{0};
    uint64_t droppedFrames{0};
    bool finalized{false};
    std::string lastError;
};

} // namespace streaming
