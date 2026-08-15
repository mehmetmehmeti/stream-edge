#pragma once

#include "streaming/StreamingSourceConfig.h"

#include <holoscan/holoscan.hpp>

class HoloscanVideoApplication : public holoscan::Application
{
public:
    explicit HoloscanVideoApplication(StreamingSourceConfig sourceConfig);

    void compose() override;

private:
    void onWindowClosed();
    void composeFileSource();
    void composeCameraSource();
    void addCameraArgs(holoscan::ArgList& args) const;
    void addVisualizerResolutionArgs(holoscan::ArgList& args) const;

    StreamingSourceConfig sourceConfig_;
};
