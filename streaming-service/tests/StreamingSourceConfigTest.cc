#include "streaming/StreamingSourceConfigLoader.h"

#include <cassert>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
class ScopedEnv
{
public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const auto* previous = std::getenv(name)) previous_ = previous;
        if (value) ::setenv(name, value, 1);
        else ::unsetenv(name);
    }
    ~ScopedEnv()
    {
        if (previous_) ::setenv(name_.c_str(), previous_->c_str(), 1);
        else ::unsetenv(name_.c_str());
    }
private:
    std::string name_;
    std::optional<std::string> previous_;
};
}

int main()
{
    ScopedEnv source("STREAMING_SOURCE_TYPE", "camera");
    ScopedEnv device("STREAMING_CAMERA_DEVICE", "/dev/video2");
    ScopedEnv width("STREAMING_CAMERA_WIDTH", "640");
    ScopedEnv height("STREAMING_CAMERA_HEIGHT", "352");
    ScopedEnv fps("STREAMING_CAMERA_FRAME_RATE", "30");
    ScopedEnv format("STREAMING_CAMERA_PIXEL_FORMAT", "YUYV");
    ScopedEnv exposure("STREAMING_CAMERA_EXPOSURE_TIME", "");
    ScopedEnv gain("STREAMING_CAMERA_GAIN", "");
    {
        ScopedEnv control("STREAMING_CAMERA_DYNAMIC_FRAME_RATE", "0");
        const auto config = loadStreamingSourceConfig();
        assert(config.type == StreamingSourceType::Camera);
        assert(config.camera.device == "/dev/video2");
        assert(config.camera.width == 640 && config.camera.height == 352);
        assert(config.camera.frameRate == 30.0F);
        assert(config.camera.pixelFormat == "YUYV");
        assert(config.camera.dynamicFrameRate.has_value() && !*config.camera.dynamicFrameRate);
    }
    {
        ScopedEnv control("STREAMING_CAMERA_DYNAMIC_FRAME_RATE", "1");
        assert(loadStreamingSourceConfig().camera.dynamicFrameRate.value());
    }
    for (const auto* value : {"", static_cast<const char*>(nullptr)})
    {
        ScopedEnv control("STREAMING_CAMERA_DYNAMIC_FRAME_RATE", value);
        assert(!loadStreamingSourceConfig().camera.dynamicFrameRate.has_value());
    }
    for (const auto* value : {"2", "false", "-1"})
    {
        ScopedEnv control("STREAMING_CAMERA_DYNAMIC_FRAME_RATE", value);
        bool rejected = false;
        try { (void)loadStreamingSourceConfig(); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
}
