#pragma once

#include <filesystem>
#include <optional>
#include <string>

enum class StreamingSourceType
{
    File,
    Camera,
    Mp4File
};

struct FileVideoSourceConfig
{
    std::filesystem::path directory;
    std::string basename;
};

struct CameraVideoSourceConfig
{
    std::filesystem::path device = "/dev/video0";
    std::optional<unsigned int> width;
    std::optional<unsigned int> height;
    std::optional<float> frameRate;
    std::optional<std::string> pixelFormat;
    std::optional<unsigned int> exposureTime;
    std::optional<unsigned int> gain;
    std::optional<bool> dynamicFrameRate;
};

struct Mp4FileVideoSourceConfig
{
    std::filesystem::path path;
};

struct StreamingSourceConfig
{
    StreamingSourceType type = StreamingSourceType::File;
    FileVideoSourceConfig file;
    CameraVideoSourceConfig camera;
    Mp4FileVideoSourceConfig mp4;
};
