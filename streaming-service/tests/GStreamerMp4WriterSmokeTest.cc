#include "streaming/GStreamerMp4Writer.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 240;
constexpr uint32_t kChannels = 4;
constexpr uint32_t kFrameCount = 30;

int fail(const std::string& message, const std::string& error = {})
{
    std::cerr << message;
    if (!error.empty())
        std::cerr << ": " << error;
    std::cerr << '\n';
    return 1;
}

std::filesystem::path outputPath()
{
    return std::filesystem::temp_directory_path() /
           "stream_edge_gstreamer_writer_smoke.mp4";
}

std::filesystem::path rgbOutputPath()
{
    return std::filesystem::temp_directory_path() /
           "stream_edge_gstreamer_writer_rgb_smoke.mp4";
}

streaming::RecordingConfig makeConfig(const std::filesystem::path& path)
{
    streaming::RecordingConfig config;
    config.outputPath = path;
    config.width = kWidth;
    config.height = kHeight;
    config.fps = 30.0;
    config.bitrateKbps = 2000;
    config.mode = streaming::RecordingMode::Cpu;
    return config;
}

streaming::RecordingFrame makeFrame(uint64_t frameNumber)
{
    streaming::RecordingFrame frame;
    frame.width = kWidth;
    frame.height = kHeight;
    frame.stride = kWidth * kChannels;
    frame.channels = kChannels;
    frame.byteCount = frame.stride * frame.height;
    frame.format = streaming::PixelFormat::RGBA8888;
    frame.dataType = streaming::FrameDataType::UInt8;
    frame.memoryLocation = streaming::MemoryLocation::Host;
    frame.frameNumber = frameNumber;
    frame.hostData.resize(frame.byteCount);

    for (uint32_t y = 0; y < kHeight; ++y)
    {
        for (uint32_t x = 0; x < kWidth; ++x)
        {
            const auto offset = static_cast<std::size_t>(y * frame.stride + x * kChannels);
            frame.hostData[offset + 0] = static_cast<uint8_t>((x + frameNumber * 3) % 256);
            frame.hostData[offset + 1] = static_cast<uint8_t>((y + frameNumber * 5) % 256);
            frame.hostData[offset + 2] = static_cast<uint8_t>((x + y + frameNumber * 7) % 256);
            frame.hostData[offset + 3] = 255;
        }
    }

    return frame;
}

streaming::RecordingFrame makeRgbFrame(uint64_t frameNumber)
{
    auto frame = makeFrame(frameNumber);
    frame.channels = 3;
    frame.stride = kWidth * frame.channels;
    frame.byteCount = frame.stride * frame.height;
    frame.format = streaming::PixelFormat::RGB888;
    frame.hostData.resize(frame.byteCount);

    for (uint32_t y = 0; y < kHeight; ++y)
    {
        for (uint32_t x = 0; x < kWidth; ++x)
        {
            const auto offset = static_cast<std::size_t>(y * frame.stride + x * frame.channels);
            frame.hostData[offset + 0] = static_cast<uint8_t>((x + frameNumber * 3) % 256);
            frame.hostData[offset + 1] = static_cast<uint8_t>((y + frameNumber * 5) % 256);
            frame.hostData[offset + 2] = static_cast<uint8_t>((x + y + frameNumber * 7) % 256);
        }
    }

    return frame;
}

int writeRecording(const std::filesystem::path& path, bool rgb)
{
    std::filesystem::remove(path);

    streaming::GStreamerMp4Writer writer;
    std::string error;

    if (!writer.start(makeConfig(path), &error))
        return fail("Failed to start GStreamer MP4 writer", error);

    if (writer.selectedPath() != streaming::RecordingPath::Cpu)
        return fail("GStreamer MP4 writer did not select CPU mode");

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        auto frame = rgb ? makeRgbFrame(i) : makeFrame(i);
        if (!writer.write(frame, &error))
            return fail("Failed to write smoke-test frame", error);
    }

    if (!writer.stop(&error))
        return fail("Failed to finalize GStreamer MP4 writer", error);

    if (!std::filesystem::exists(path))
        return fail("GStreamer smoke-test MP4 was not created");

    if (std::filesystem::file_size(path) < 1024)
        return fail("GStreamer smoke-test MP4 is unexpectedly small");

    std::filesystem::remove(path);
    return 0;
}
} // namespace

int main()
{
    if (const auto result = writeRecording(outputPath(), false); result != 0)
        return result;

    return writeRecording(rgbOutputPath(), true);
}
