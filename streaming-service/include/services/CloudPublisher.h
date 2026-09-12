#pragma once

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace streaming
{
class FrameBridge;
}

class FrameBridgeShmPublisher;

class CloudPublisher
{
public:
    static CloudPublisher &instance();
    ~CloudPublisher();

    bool start(std::string *message = nullptr);
    bool startFilePublisher(const std::filesystem::path& inputFile,
                            const std::string& streamNameOverride = {},
                            std::string *message = nullptr);
    bool stop(std::string *message = nullptr);
    bool isEnabled() const;
    Json::Value statusJson() const;

private:
    struct SessionConfig
    {
        std::string sessionId;
        std::string deviceId;
        std::string streamName;
        std::string whipUrl;
        std::string publishToken;
        std::string viewUrl;
        bool reused = false;
    };

    struct RuntimeConfig
    {
        bool enabled = false;
        bool required = false;
        std::string backendBaseUrl;
        std::string internalToken;
        std::string deviceId;
        std::string commandTemplate;
        std::string directWhipUrl;
        std::string directPublishToken;
        std::string directViewUrl;
        std::string directStreamName;
        std::string directSessionId;
        std::string streamNameOverride;
        std::filesystem::path directInputFile;
        std::filesystem::path whipClientPath;
        bool loopDirectInputFile = true;
        bool logCommandOutput = false;
        int pollIntervalMs = 100;
        std::string shmSocketPath;
        std::uint32_t shmSizeBytes = 16U * 1024U * 1024U;
        std::uint32_t frameRateNumerator = 30;
        std::uint32_t frameRateDenominator = 1;
        int frameStallTimeoutMs = 10000;
    };

    CloudPublisher();
    CloudPublisher(const CloudPublisher &) = delete;
    CloudPublisher &operator=(const CloudPublisher &) = delete;

    RuntimeConfig loadRuntimeConfig() const;
    bool startWithConfig(RuntimeConfig config, std::string *message);
    bool fetchSessionConfig(const RuntimeConfig &config,
                            SessionConfig &session,
                            std::string &error) const;
    bool notifySessionStopped(const RuntimeConfig &config,
                              const std::string &sessionId,
                              std::string &error) const;
    bool launchExternalPublisher(const RuntimeConfig &config,
                                 const SessionConfig &session,
                                 const Json::Value& bridgeJson,
                                 int& childPid,
                                 std::string &error);
    void monitorLoop();
    void stopExternalPublisherLocked();
    static std::string buildExternalCommand(const std::string &commandTemplate,
                                            const SessionConfig &session,
                                            const Json::Value& bridgeJson);

    mutable std::mutex mutex_;
    std::shared_ptr<streaming::FrameBridge> frameBridge_;
    std::unique_ptr<FrameBridgeShmPublisher> framePublisher_;
    std::thread monitorThread_;
    bool running_ = false;
    bool stopRequested_ = false;
    std::string state_{"disabled"};
    std::string lastError_;
    std::string lastMessage_;
    RuntimeConfig runtimeConfig_{};
    std::optional<SessionConfig> session_;
    uint64_t consumedFrameCount_ = 0;
    uint64_t lastFrameNumber_ = 0;
    uint64_t lastFrameTimestampNs_ = 0;
    int childPid_ = -1;
};
