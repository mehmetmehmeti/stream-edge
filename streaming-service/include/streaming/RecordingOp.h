#pragma once

#include "streaming/FrameStagingPool.h"
#include "streaming/RecordingEngine.h"

#include <cuda_runtime_api.h>

#include <cstdint>
#include <memory>

#include <holoscan/holoscan.hpp>

class RecordingOp final : public holoscan::Operator
{
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS(RecordingOp)
    RecordingOp();
    ~RecordingOp() override;

    void setup(holoscan::OperatorSpec& spec) override;

    void compute(holoscan::InputContext& opInput,
                 holoscan::OutputContext& opOutput,
                 holoscan::ExecutionContext& context) override;

    static std::shared_ptr<streaming::RecordingEngine> sharedRecordingEngine();

private:
    static std::shared_ptr<streaming::RecordingEngine> defaultRecordingEngine();
    cudaStream_t stagingStream();

    std::shared_ptr<streaming::RecordingEngine> recordingEngine_{defaultRecordingEngine()};
    std::shared_ptr<streaming::FrameStagingPool> stagingPool_{
        std::make_shared<streaming::FrameStagingPool>(3)};
    cudaStream_t stagingStream_{nullptr};
    uint64_t receivedFrameCount_{0};
};
