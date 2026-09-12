#include "streaming/StreamingSourceConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;

std::string normalizedSourceType(const char* sourceType)
{
    if (sourceType == nullptr || *sourceType == '\0')
        return "file";

    std::string normalized = sourceType;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return normalized;
}

StreamingSourceType loadStreamingSourceType()
{
    const auto sourceType = normalizedSourceType(std::getenv("STREAMING_SOURCE_TYPE"));

    if (sourceType == "file")
        return StreamingSourceType::File;

    if (sourceType == "camera")
        return StreamingSourceType::Camera;

    if (sourceType == "mp4" || sourceType == "mp4-file" || sourceType == "mp4_file")
        return StreamingSourceType::Mp4File;

    throw std::runtime_error(
        "Unsupported STREAMING_SOURCE_TYPE: " + sourceType +
        " (expected file, camera, or mp4)");
}

std::optional<unsigned int> loadOptionalUnsignedEnv(const char* name)
{
    const auto* rawValue = std::getenv(name);
    if (rawValue == nullptr || *rawValue == '\0')
        return std::nullopt;

    std::string value = rawValue;
    std::size_t parsedLength = 0;
    unsigned long parsedValue = 0;
    try
    {
        parsedValue = std::stoul(value, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(std::string("Invalid unsigned integer for ") + name + ": " +
                                 value);
    }

    if (parsedLength != value.size() || parsedValue > std::numeric_limits<unsigned int>::max())
        throw std::runtime_error(std::string("Invalid unsigned integer for ") + name + ": " +
                                 value);

    return static_cast<unsigned int>(parsedValue);
}

std::optional<float> loadOptionalPositiveFloatEnv(const char* name)
{
    const auto* rawValue = std::getenv(name);
    if (rawValue == nullptr || *rawValue == '\0')
        return std::nullopt;

    std::string value = rawValue;
    std::size_t parsedLength = 0;
    float parsedValue = 0.0F;
    try
    {
        parsedValue = std::stof(value, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(std::string("Invalid positive number for ") + name + ": " +
                                 value);
    }

    if (parsedLength != value.size() || !std::isfinite(parsedValue) || parsedValue <= 0.0F)
        throw std::runtime_error(std::string("Invalid positive number for ") + name + ": " +
                                 value);

    return parsedValue;
}

FileVideoSourceConfig loadFileVideoSourceConfig()
{
    const auto* sourceDirectory = std::getenv("STREAMING_SOURCE_DIR");
    const auto* sourceBasename = std::getenv("STREAMING_SOURCE_BASENAME");

    if (sourceDirectory == nullptr || *sourceDirectory == '\0')
        throw std::runtime_error("Set STREAMING_SOURCE_DIR before starting the stream");

    FileVideoSourceConfig config;
    config.directory = fs::path(sourceDirectory);
    config.basename = (sourceBasename != nullptr && *sourceBasename != '\0')
                          ? sourceBasename
                          : config.directory.filename().string();

    if (config.basename.empty())
        throw std::runtime_error(
            "Unable to determine the video basename; set STREAMING_SOURCE_BASENAME explicitly");

    if (!fs::exists(config.directory))
        throw std::runtime_error("File video source directory not found: " +
                                 config.directory.string());

    if (!fs::is_directory(config.directory))
        throw std::runtime_error("File video source path is not a directory: " +
                                 config.directory.string());

    return config;
}

CameraVideoSourceConfig loadCameraVideoSourceConfig()
{
    const auto* cameraDevice = std::getenv("STREAMING_CAMERA_DEVICE");

    CameraVideoSourceConfig config;
    if (cameraDevice != nullptr && *cameraDevice != '\0')
        config.device = fs::path(cameraDevice);

    if (config.device.empty())
        throw std::runtime_error("Camera device path is empty");

    config.width = loadOptionalUnsignedEnv("STREAMING_CAMERA_WIDTH");
    config.height = loadOptionalUnsignedEnv("STREAMING_CAMERA_HEIGHT");
    config.frameRate = loadOptionalPositiveFloatEnv("STREAMING_CAMERA_FRAME_RATE");
    config.exposureTime = loadOptionalUnsignedEnv("STREAMING_CAMERA_EXPOSURE_TIME");
    config.gain = loadOptionalUnsignedEnv("STREAMING_CAMERA_GAIN");

    const auto* pixelFormat = std::getenv("STREAMING_CAMERA_PIXEL_FORMAT");
    if (pixelFormat != nullptr && *pixelFormat != '\0')
        config.pixelFormat = pixelFormat;

    if (config.width && *config.width == 0)
        throw std::runtime_error("STREAMING_CAMERA_WIDTH must be greater than 0 when set");

    if (config.height && *config.height == 0)
        throw std::runtime_error("STREAMING_CAMERA_HEIGHT must be greater than 0 when set");

    return config;
}

Mp4FileVideoSourceConfig loadMp4FileVideoSourceConfig()
{
    const auto* sourceFile = std::getenv("STREAMING_SOURCE_FILE");
    if (sourceFile == nullptr || *sourceFile == '\0')
        throw std::runtime_error("Set STREAMING_SOURCE_FILE before starting an MP4 stream");

    Mp4FileVideoSourceConfig config;
    config.path = fs::path(sourceFile);

    if (!fs::exists(config.path))
        throw std::runtime_error("MP4 source file not found: " + config.path.string());

    if (!fs::is_regular_file(config.path))
        throw std::runtime_error("MP4 source path is not a file: " + config.path.string());

    return config;
}
} // namespace

StreamingSourceConfig loadStreamingSourceConfig()
{
    StreamingSourceConfig config;
    config.type = loadStreamingSourceType();

    switch (config.type)
    {
    case StreamingSourceType::File:
        config.file = loadFileVideoSourceConfig();
        break;
    case StreamingSourceType::Camera:
        config.camera = loadCameraVideoSourceConfig();
        break;
    case StreamingSourceType::Mp4File:
        config.mp4 = loadMp4FileVideoSourceConfig();
        break;
    }

    return config;
}
