#pragma once

#include "streaming/RecordingWriter.h"

#include <cstdint>
#include <string>
#include <vector>

typedef struct _GstBus GstBus;
typedef struct _GstCudaContext GstCudaContext;
typedef struct _GstCudaStream GstCudaStream;
typedef struct _GstElement GstElement;

namespace streaming
{

class GStreamerMp4Writer final : public RecordingWriter
{
public:
    GStreamerMp4Writer() = default;
    ~GStreamerMp4Writer() override;

    bool start(const RecordingConfig& config, std::string* errorMessage) override;
    bool write(const RecordingFrame& frame, std::string* errorMessage) override;
    bool stop(std::string* errorMessage) override;
    RecordingPath selectedPath() const override;

private:
    bool startPipeline(const char* encoderName,
                       RecordingPath selectedPath,
                       bool useCudaMemory,
                       bool useJetsonNvmmUpload,
                       std::string* errorMessage);
    bool copyFrameToHost(const RecordingFrame& frame,
                         std::vector<uint8_t>& output,
                         std::string* errorMessage) const;
    bool pushCudaFrame(const RecordingFrame& frame, std::string* errorMessage);
    uint64_t ptsForFrame(const RecordingFrame& frame);
    bool ensureCudaContext(int32_t deviceId, std::string* errorMessage);
    void clearCudaContext();
    void clearPipeline();

    GstElement* pipeline_{nullptr};
    GstElement* appsrc_{nullptr};
    GstBus* bus_{nullptr};
    GstCudaContext* cudaContext_{nullptr};
    GstCudaStream* cudaStream_{nullptr};
    RecordingConfig config_;
    RecordingPath selectedPath_{RecordingPath::None};
    bool usesCudaMemory_{false};
    uint64_t pushedFrames_{0};
    uint64_t frameDurationNs_{0};
    bool hasFirstFrameTimestamp_{false};
    uint64_t firstFrameTimestampNs_{0};
};

} // namespace streaming
