#pragma once

#include "streaming/RecordingConfig.h"
#include "streaming/RecordingDiagnostics.h"
#include "streaming/RecordingFrame.h"

#include <string>

namespace streaming
{

class RecordingWriter
{
public:
    virtual ~RecordingWriter() = default;

    virtual bool start(const RecordingConfig& config, std::string* errorMessage) = 0;
    virtual bool write(const RecordingFrame& frame, std::string* errorMessage) = 0;
    virtual bool stop(std::string* errorMessage) = 0;
    virtual RecordingPath selectedPath() const = 0;
};

} // namespace streaming
