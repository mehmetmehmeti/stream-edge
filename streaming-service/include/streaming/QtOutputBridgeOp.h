#pragma once

#include "streaming/FrameBridge.h"
#include "streaming/FrameStagingPool.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <holoscan/holoscan.hpp>

class QtOutputBridgeOp final : public holoscan::Operator
{
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS(QtOutputBridgeOp)
    QtOutputBridgeOp();

    void setup(holoscan::OperatorSpec& spec) override;

    void compute(holoscan::InputContext& opInput,
                 holoscan::OutputContext& opOutput,
                 holoscan::ExecutionContext& context) override;

    static std::shared_ptr<streaming::FrameBridge> sharedFrameBridge();

private:
    static std::shared_ptr<streaming::FrameBridge> defaultFrameBridge();

    std::shared_ptr<streaming::FrameBridge> frameBridge_{defaultFrameBridge()};
    std::shared_ptr<streaming::FrameStagingPool> stagingPool_{
        std::make_shared<streaming::FrameStagingPool>(3)};
    bool hasLoggedFrameMetadata_ = false;
    std::vector<int64_t> lastShape_;
    std::vector<int64_t> lastStrides_;
    std::string lastDataType_;
    std::string lastDevice_;
    uint64_t receivedFrameCount_ = 0;
    uint64_t invalidFrameCount_ = 0;
    uint64_t stagedFrameCount_ = 0;
    uint64_t droppedFrameCount_ = 0;
};
