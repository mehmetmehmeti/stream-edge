#include "streaming/StreamingSourceHealth.h"

#include "streaming/StreamingSourceConfig.h"
#include "streaming/StreamingSourceConfigLoader.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;

bool checkFileSource(const FileVideoSourceConfig& config, std::string* errorMessage)
{
    if (config.directory.empty())
    {
        if (errorMessage)
            *errorMessage = "File video source directory is empty";
        return false;
    }

    if (!fs::exists(config.directory))
    {
        if (errorMessage)
            *errorMessage = "File video source directory not found: " + config.directory.string();
        return false;
    }

    if (!fs::is_directory(config.directory))
    {
        if (errorMessage)
            *errorMessage = "File video source path is not a directory: " + config.directory.string();
        return false;
    }

    return true;
}

bool checkCameraSource(const CameraVideoSourceConfig& config, std::string* errorMessage)
{
    if (config.device.empty())
    {
        if (errorMessage)
            *errorMessage = "Camera device path is empty";
        return false;
    }

    if (!fs::exists(config.device))
    {
        if (errorMessage)
            *errorMessage = "Camera device not found: " + config.device.string();
        return false;
    }

    if (!fs::is_character_file(config.device))
    {
        if (errorMessage)
            *errorMessage = "Camera source is not a character device: " + config.device.string();
        return false;
    }

    const int fd = ::open(config.device.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        if (errorMessage)
            *errorMessage = "Camera device is not readable: " + config.device.string() + " (" +
                            std::strerror(errno) + ")";
        return false;
    }

    ::close(fd);
    return true;
}
} // namespace

namespace streaming
{

bool isStreamingSourceSignalAvailable(std::string* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    StreamingSourceConfig config;
    try
    {
        config = loadStreamingSourceConfig();
    }
    catch (const std::exception& error)
    {
        if (errorMessage)
            *errorMessage = error.what();
        return false;
    }

    switch (config.type)
    {
    case StreamingSourceType::File:
        return checkFileSource(config.file, errorMessage);
    case StreamingSourceType::Camera:
        return checkCameraSource(config.camera, errorMessage);
    }

    if (errorMessage)
        *errorMessage = "Unsupported streaming source type";
    return false;
}

} // namespace streaming
