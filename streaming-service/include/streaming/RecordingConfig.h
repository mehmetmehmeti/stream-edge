#pragma once

#include <cstdint>
#include <filesystem>

namespace streaming
{

enum class RecordingMode
{
    Auto,
    Gpu,
    Cpu
};

enum class RecordingCodec
{
    H264
};

struct RecordingConfig
{
    std::filesystem::path outputPath;
    uint32_t width{0};
    uint32_t height{0};
    double fps{30.0};
    uint32_t bitrateKbps{8000};
    uint32_t queueDepth{8};
    RecordingMode mode{RecordingMode::Auto};
    RecordingCodec codec{RecordingCodec::H264};
};

inline bool isValid(const RecordingConfig& config)
{
    return !config.outputPath.empty() &&
           config.width > 0 &&
           config.height > 0 &&
           config.fps > 0.0 &&
           config.bitrateKbps > 0 &&
           config.queueDepth > 0;
}

} // namespace streaming
