#include "services/CloudPublisher.h"

#include "services/FrameBridgeShmPublisher.h"
#include "streaming/FrameBridge.h"
#include "streaming/QtOutputBridgeOp.h"
#include "streaming/VideoFrameHandle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

extern char **environ;

namespace
{
std::string trim(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string getEnvOrDefault(const char *name, const std::string &defaultValue)
{
    const auto *value = std::getenv(name);
    if (value == nullptr || std::string(value).empty())
        return defaultValue;

    return value;
}

bool getEnvAsBool(const char *name, bool defaultValue)
{
    const auto raw = trim(getEnvOrDefault(name, defaultValue ? "1" : "0"));
    return raw == "1" || raw == "true" || raw == "TRUE" || raw == "yes";
}

int getEnvAsInt(const char *name, int defaultValue)
{
    const auto raw = trim(getEnvOrDefault(name, std::to_string(defaultValue)));
    if (raw.empty())
        return defaultValue;

    try
    {
        return std::stoi(raw);
    }
    catch (const std::exception &)
    {
        return defaultValue;
    }
}

std::uint32_t getEnvAsUint32(const char* name, std::uint32_t defaultValue)
{
    const auto parsed = getEnvAsInt(name, static_cast<int>(defaultValue));
    return parsed > 0 ? static_cast<std::uint32_t>(parsed) : defaultValue;
}

std::optional<double> getOptionalPositiveEnvDouble(const char* name)
{
    const auto* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0')
        return std::nullopt;

    try
    {
        const auto value = std::stod(raw);
        if (std::isfinite(value) && value > 0.0)
            return value;
    }
    catch (const std::exception&)
    {
    }
    return std::nullopt;
}

std::string shellQuote(const std::string &value)
{
    std::string escaped = "'";
    for (const auto ch : value)
    {
        if (ch == '\'')
            escaped += "'\\''";
        else
            escaped += ch;
    }
    escaped += "'";
    return escaped;
}

std::string replaceAll(std::string value,
                       const std::string &needle,
                       const std::string &replacement)
{
    std::size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos)
    {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

bool runShellCommandWithOutput(const std::string &command,
                               std::string &output,
                               int &exitCode)
{
    output.clear();
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
        return false;

    char buffer[4096];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr)
        output += buffer;

    const auto status = pclose(pipe);
    if (WIFEXITED(status))
        exitCode = WEXITSTATUS(status);
    else
        exitCode = -1;

    return true;
}

std::uint64_t steadyNowNs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

CloudPublisher::CloudPublisher()
    : frameBridge_(QtOutputBridgeOp::sharedFrameBridge()),
      framePublisher_(std::make_unique<FrameBridgeShmPublisher>())
{
}

CloudPublisher::~CloudPublisher()
{
    std::string ignored;
    (void)stop(&ignored);
}

CloudPublisher &CloudPublisher::instance()
{
    static CloudPublisher publisher;
    return publisher;
}

CloudPublisher::RuntimeConfig CloudPublisher::loadRuntimeConfig() const
{
    RuntimeConfig config;
    config.enabled = getEnvAsBool("CLOUD_PUBLISH_ENABLED", false);
    config.required = getEnvAsBool("CLOUD_PUBLISH_REQUIRED", false);
    config.backendBaseUrl = trim(getEnvOrDefault("BACKEND_API_BASE_URL", "http://127.0.0.1:8080"));
    config.internalToken = trim(getEnvOrDefault("EDGE_INTERNAL_API_TOKEN", ""));
    config.deviceId = trim(getEnvOrDefault("EDGE_DEVICE_ID", ""));
    config.commandTemplate = getEnvOrDefault("MEDIA_PUBLISH_COMMAND_TEMPLATE", "");
    config.directWhipUrl = trim(getEnvOrDefault("MEDIA_PUBLISH_WHIP_URL", ""));
    config.directPublishToken = trim(getEnvOrDefault("MEDIA_PUBLISH_TOKEN", ""));
    config.directViewUrl = trim(getEnvOrDefault("MEDIA_PUBLISH_VIEW_URL", ""));
    config.directStreamName = trim(getEnvOrDefault("MEDIA_PUBLISH_STREAM_NAME", ""));
    config.directSessionId = trim(getEnvOrDefault("MEDIA_PUBLISH_SESSION_ID", ""));
    config.logCommandOutput = getEnvAsBool("CLOUD_PUBLISH_LOG_COMMAND_OUTPUT", false);
    config.pollIntervalMs = getEnvAsInt("CLOUD_PUBLISH_POLL_INTERVAL_MS", 100);
    config.shmSocketPath = trim(getEnvOrDefault("CLOUD_PUBLISH_SHM_SOCKET_PATH",
                                                "/tmp/streaming-service-publisher.sock"));
    config.shmSizeBytes = getEnvAsUint32("CLOUD_PUBLISH_SHM_SIZE_BYTES", 16U * 1024U * 1024U);
    config.frameRateNumerator = getEnvAsUint32("CLOUD_PUBLISH_FRAME_RATE_NUM", 30U);
    config.frameRateDenominator = getEnvAsUint32("CLOUD_PUBLISH_FRAME_RATE_DEN", 1U);
    config.frameStallTimeoutMs = getEnvAsInt("CLOUD_PUBLISH_FRAME_STALL_TIMEOUT_MS", 10000);
    if (config.pollIntervalMs < 20)
        config.pollIntervalMs = 20;
    if (config.frameStallTimeoutMs > 0 && config.frameStallTimeoutMs < 1000)
        config.frameStallTimeoutMs = 1000;
    return config;
}

bool CloudPublisher::isEnabled() const
{
    return loadRuntimeConfig().enabled;
}

bool CloudPublisher::fetchSessionConfig(const RuntimeConfig &config,
                                        SessionConfig &session,
                                        std::string &error) const
{
    if (!config.directWhipUrl.empty())
    {
        const auto streamName = config.directStreamName.empty()
                                    ? config.deviceId + "-stream"
                                    : config.directStreamName;
        session.sessionId = config.directSessionId.empty() ? streamName : config.directSessionId;
        session.deviceId = config.deviceId;
        session.streamName = streamName;
        session.whipUrl = config.directWhipUrl;
        session.publishToken = config.directPublishToken;
        session.viewUrl = config.directViewUrl;
        session.reused = false;
        return true;
    }

    Json::Value body;
    body["device_id"] = config.deviceId;

    Json::StreamWriterBuilder writer;
    const auto payload = Json::writeString(writer, body);

    std::ostringstream command;
    command << "curl -sS -X POST "
            << shellQuote(config.backendBaseUrl + "/internal/stream/publish-sessions/current")
            << " -H 'Content-Type: application/json'"
            << " -H " << shellQuote("Authorization: Bearer " + config.internalToken)
            << " --data " << shellQuote(payload);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(command.str(), output, exitCode))
    {
        error = "Failed to invoke curl for publish session request";
        return false;
    }
    if (exitCode != 0)
    {
        error = "Publish session request failed with exit code " + std::to_string(exitCode);
        return false;
    }

    Json::CharReaderBuilder readerBuilder;
    Json::Value response;
    std::string parseErrors;
    std::istringstream input(output);
    if (!Json::parseFromStream(readerBuilder, input, &response, &parseErrors))
    {
        error = "Invalid JSON from backend publish session endpoint";
        return false;
    }

    if (!response.isObject() || !response["session_id"].isString() ||
        !response["whip_url"].isString() || !response["publish_token"].isString())
    {
        error = response["message"].isString()
                    ? response["message"].asString()
                    : "Backend publish session payload is incomplete";
        return false;
    }

    session.sessionId = response["session_id"].asString();
    session.deviceId = response["device_id"].asString();
    session.streamName = response["stream_name"].asString();
    session.whipUrl = response["whip_url"].asString();
    session.publishToken = response["publish_token"].asString();
    session.viewUrl = response["view_url"].asString();
    session.reused = response["reused"].asBool();
    return true;
}

bool CloudPublisher::notifySessionStopped(const RuntimeConfig &config,
                                          const std::string &sessionId,
                                          std::string &error) const
{
    if (!config.directWhipUrl.empty())
        return true;

    if (sessionId.empty() || config.backendBaseUrl.empty() || config.internalToken.empty())
        return true;

    std::ostringstream command;
    command << "curl -sS -X POST "
            << shellQuote(config.backendBaseUrl + "/internal/stream/publish-sessions/" +
                          sessionId + "/stop")
            << " -H " << shellQuote("Authorization: Bearer " + config.internalToken);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(command.str(), output, exitCode))
    {
        error = "Failed to invoke curl for publish session stop request";
        return false;
    }
    if (exitCode != 0)
    {
        error = "Publish session stop request failed with exit code " +
                std::to_string(exitCode);
        return false;
    }

    return true;
}

std::string CloudPublisher::buildExternalCommand(const std::string &commandTemplate,
                                                 const SessionConfig &session,
                                                 const Json::Value& bridgeJson)
{
    auto command = replaceAll(commandTemplate, "{session_id}", session.sessionId);
    command = replaceAll(command, "{{SESSION_ID}}", session.sessionId);
    command = replaceAll(command, "{device_id}", session.deviceId);
    command = replaceAll(command, "{{DEVICE_ID}}", session.deviceId);
    command = replaceAll(command, "{stream_name}", session.streamName);
    command = replaceAll(command, "{{STREAM_NAME}}", session.streamName);
    command = replaceAll(command, "{whip_url}", session.whipUrl);
    command = replaceAll(command, "{{WHIP_URL}}", session.whipUrl);
    command = replaceAll(command, "{publish_token}", session.publishToken);
    command = replaceAll(command, "{{PUBLISH_TOKEN}}", session.publishToken);
    command = replaceAll(command, "{view_url}", session.viewUrl);
    command = replaceAll(command, "{{VIEW_URL}}", session.viewUrl);
    if (bridgeJson.isObject())
    {
        const auto socketPath = bridgeJson["socket_path"].asString();
        const auto gstFormat = bridgeJson["gst_format"].asString();
        const auto width = std::to_string(bridgeJson["width"].asUInt());
        const auto height = std::to_string(bridgeJson["height"].asUInt());
        const auto fpsNum = std::to_string(bridgeJson["frame_rate_num"].asUInt());
        const auto fpsDen = std::to_string(bridgeJson["frame_rate_den"].asUInt());
        const auto fpsNumerator = std::max(bridgeJson["frame_rate_num"].asUInt(), 1U);
        const auto fpsDenominator = std::max(bridgeJson["frame_rate_den"].asUInt(), 1U);
        const auto keyframeInterval =
            std::to_string(std::max(1U, (fpsNumerator + fpsDenominator - 1U) / fpsDenominator));

        command = replaceAll(command, "{shm_socket_path}", socketPath);
        command = replaceAll(command, "{{SHM_SOCKET_PATH}}", socketPath);
        command = replaceAll(command, "{gst_video_format}", gstFormat);
        command = replaceAll(command, "{{GST_VIDEO_FORMAT}}", gstFormat);
        command = replaceAll(command, "{frame_width}", width);
        command = replaceAll(command, "{{FRAME_WIDTH}}", width);
        command = replaceAll(command, "{frame_height}", height);
        command = replaceAll(command, "{{FRAME_HEIGHT}}", height);
        command = replaceAll(command, "{frame_rate_num}", fpsNum);
        command = replaceAll(command, "{{FRAME_RATE_NUM}}", fpsNum);
        command = replaceAll(command, "{frame_rate_den}", fpsDen);
        command = replaceAll(command, "{{FRAME_RATE_DEN}}", fpsDen);
        command = replaceAll(command, "{keyframe_interval}", keyframeInterval);
        command = replaceAll(command, "{{KEYFRAME_INTERVAL}}", keyframeInterval);
    }
    return command;
}

bool CloudPublisher::launchExternalPublisher(const RuntimeConfig &config,
                                             const SessionConfig &session,
                                             const Json::Value& bridgeJson,
                                             int& childPid,
                                             std::string &error)
{
    childPid = -1;
    if (trim(config.commandTemplate).empty())
    {
        error = "No MEDIA_PUBLISH_COMMAND_TEMPLATE configured";
        return false;
    }

    const auto command = "exec " + buildExternalCommand(config.commandTemplate, session, bridgeJson);

    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
    {
        error = "posix_spawnattr_init() failed while starting external publisher";
        return false;
    }

    const short flags = POSIX_SPAWN_SETPGROUP;
    (void)posix_spawnattr_setflags(&attributes, flags);
    (void)posix_spawnattr_setpgroup(&attributes, 0);

    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_t* fileActionsPtr = nullptr;
    if (!config.logCommandOutput)
    {
        if (posix_spawn_file_actions_init(&fileActions) != 0)
        {
            posix_spawnattr_destroy(&attributes);
            error = "posix_spawn_file_actions_init() failed while starting external publisher";
            return false;
        }
        fileActionsPtr = &fileActions;
        (void)posix_spawn_file_actions_addopen(
            fileActionsPtr, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        (void)posix_spawn_file_actions_adddup2(
            fileActionsPtr, STDOUT_FILENO, STDERR_FILENO);
    }

    pid_t pid = -1;
    char shell[] = "sh";
    char option[] = "-c";
    char* argv[] = {
        shell,
        option,
        const_cast<char*>(command.c_str()),
        nullptr,
    };
    const auto spawnResult = posix_spawn(
        &pid, "/bin/sh", fileActionsPtr, &attributes, argv, environ);
    if (fileActionsPtr != nullptr)
        posix_spawn_file_actions_destroy(fileActionsPtr);
    posix_spawnattr_destroy(&attributes);

    if (spawnResult != 0)
    {
        error = std::string("posix_spawn() failed while starting external publisher: ") +
                std::strerror(spawnResult);
        return false;
    }

    childPid = static_cast<int>(pid);
    return true;
}

bool CloudPublisher::start(std::string *message)
{
    const auto config = loadRuntimeConfig();

    {
        std::lock_guard lock(mutex_);
        if (running_)
        {
            if (message)
                *message = "Cloud publisher is already running";
            return true;
        }

        runtimeConfig_ = config;
        lastError_.clear();
        lastMessage_.clear();
        consumedFrameCount_ = 0;
        lastFrameNumber_ = 0;
        lastFrameTimestampNs_ = 0;
        session_.reset();
        stopRequested_ = false;
        childPid_ = -1;
    }

    if (!config.enabled)
    {
        std::lock_guard lock(mutex_);
        state_ = "disabled";
        lastMessage_ = "Cloud publishing is disabled";
        if (message)
            *message = lastMessage_;
        return true;
    }

    if (config.deviceId.empty())
    {
        const auto error = "Cloud publishing requires EDGE_DEVICE_ID";
        std::lock_guard lock(mutex_);
        state_ = "failed";
        lastError_ = error;
        if (message)
            *message = error;
        return false;
    }

    if (config.directWhipUrl.empty() &&
        (config.internalToken.empty() || config.backendBaseUrl.empty()))
    {
        const auto error = "Cloud publishing requires either MEDIA_PUBLISH_WHIP_URL "
                           "or EDGE_INTERNAL_API_TOKEN and BACKEND_API_BASE_URL";
        std::lock_guard lock(mutex_);
        state_ = "failed";
        lastError_ = error;
        if (message)
            *message = error;
        return false;
    }

    if (const auto captureFps = getOptionalPositiveEnvDouble("STREAMING_CAMERA_FRAME_RATE"))
    {
        const auto exportFps = static_cast<double>(config.frameRateNumerator) /
                               static_cast<double>(config.frameRateDenominator);
        if (std::abs(*captureFps - exportFps) > 0.01)
        {
            const auto error = "Capture FPS (" + std::to_string(*captureFps) +
                               ") does not match cloud export FPS (" +
                               std::to_string(exportFps) + ")";
            std::lock_guard lock(mutex_);
            state_ = "failed";
            lastError_ = error;
            if (message)
                *message = error;
            return false;
        }
    }

    SessionConfig session;
    std::string error;
    if (!fetchSessionConfig(config, session, error))
    {
        std::lock_guard lock(mutex_);
        state_ = "failed";
        lastError_ = error;
        if (message)
            *message = error;
        return false;
    }

    if (!framePublisher_)
        framePublisher_ = std::make_unique<FrameBridgeShmPublisher>();

    FrameBridgeShmPublisher::Config bridgeConfig;
    bridgeConfig.socketPath = config.shmSocketPath;
    bridgeConfig.frameRateNumerator = config.frameRateNumerator;
    bridgeConfig.frameRateDenominator = config.frameRateDenominator;
    bridgeConfig.shmSizeBytes = config.shmSizeBytes;

    if (!framePublisher_->start(bridgeConfig, error))
    {
        std::lock_guard lock(mutex_);
        state_ = "failed";
        lastError_ = error;
        if (message)
            *message = error;
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        session_ = session;
        running_ = true;
        state_ = "waiting_for_frame";
        lastMessage_ = "Cloud publish session is ready; waiting for the first frame";
        monitorThread_ = std::thread([this]() { monitorLoop(); });
        if (message)
            *message = lastMessage_;
    }

    return true;
}

void CloudPublisher::stopExternalPublisherLocked()
{
    if (childPid_ <= 0)
        return;

    const auto pid = childPid_;
    childPid_ = -1;
    ::kill(-pid, SIGTERM);
    ::kill(pid, SIGTERM);

    int status = 0;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    (void)waitpid(pid, &status, 0);
}

bool CloudPublisher::stop(std::string *message)
{
    std::thread threadToJoin;
    RuntimeConfig config;
    std::optional<SessionConfig> session;

    {
        std::lock_guard lock(mutex_);
        config = runtimeConfig_;
        session = session_;
        stopRequested_ = true;
        stopExternalPublisherLocked();

        if (monitorThread_.joinable())
            threadToJoin = std::move(monitorThread_);

        running_ = false;
        state_ = config.enabled ? "stopped" : "disabled";
        lastMessage_ = "Cloud publisher stopped";
    }

    if (threadToJoin.joinable())
        threadToJoin.join();

    {
        std::lock_guard lock(mutex_);
        if (framePublisher_)
            framePublisher_->stop();
    }

    std::string stopError;
    if (session.has_value())
        (void)notifySessionStopped(config, session->sessionId, stopError);

    {
        std::lock_guard lock(mutex_);
        session_.reset();
        if (!stopError.empty())
            lastError_ = stopError;
        if (message)
            *message = lastMessage_;
    }

    return true;
}

void CloudPublisher::monitorLoop()
{
    bool launchAttempted = false;
    std::uint64_t lastConsumedFrameNumber = 0;
    auto lastFrameProgressAt = std::chrono::steady_clock::now();
    bool sawFrame = false;

    const auto failAndStop = [this](const std::string& errorMessage) {
        RuntimeConfig failedConfig;
        std::optional<SessionConfig> failedSession;

        {
            std::lock_guard lock(mutex_);
            failedConfig = runtimeConfig_;
            failedSession = session_;
            running_ = false;
            state_ = "failed";
            lastError_ = errorMessage;
            stopExternalPublisherLocked();
            if (framePublisher_)
                framePublisher_->stop();
        }

        std::string ignored;
        if (failedSession.has_value())
            (void)notifySessionStopped(failedConfig, failedSession->sessionId, ignored);
    };

    while (true)
    {
        RuntimeConfig config;
        int childPid = -1;
        std::optional<SessionConfig> session;

        {
            std::lock_guard lock(mutex_);
            if (stopRequested_)
                break;

            config = runtimeConfig_;
            childPid = childPid_;
            session = session_;
        }

        if (childPid > 0)
        {
            int status = 0;
            const auto result = waitpid(childPid, &status, WNOHANG);
            if (result == childPid)
            {
                {
                    std::lock_guard lock(mutex_);
                    if (childPid_ == childPid)
                        childPid_ = -1;
                }
                std::string reason = "External publisher process exited unexpectedly";
                if (WIFEXITED(status))
                    reason += " (exit code " + std::to_string(WEXITSTATUS(status)) + ")";
                else if (WIFSIGNALED(status))
                    reason += " (signal " + std::to_string(WTERMSIG(status)) + ")";
                failAndStop(reason);
                break;
            }
        }

        streaming::VideoFrameHandle frame;
        const auto waitInterval = std::chrono::milliseconds(config.pollIntervalMs);
        if (frameBridge_ &&
            frameBridge_->waitForFrameAfter(lastConsumedFrameNumber, frame, waitInterval))
        {
            lastConsumedFrameNumber = frame.frameNumber;
            std::string publishError;
            bool pushed = false;
            bool shouldFail = false;

            if (framePublisher_)
            {
                pushed = framePublisher_->pushFrame(frame, publishError);
                if (!pushed)
                    shouldFail = true;
            }

            {
                std::lock_guard lock(mutex_);
                ++consumedFrameCount_;
                lastFrameNumber_ = frame.frameNumber;
                lastFrameTimestampNs_ = frame.timestampNs;
                if (pushed && state_ == "waiting_for_frame")
                    state_ = "bridge_running";
            }

            frameBridge_->releaseFrame(frame.frameNumber);

            if (shouldFail)
            {
                failAndStop(publishError.empty()
                                ? "Failed to publish frame to shared-memory bridge"
                                : publishError);
                break;
            }

            sawFrame = true;
            lastFrameProgressAt = std::chrono::steady_clock::now();

            if (!launchAttempted && session.has_value())
            {
                const auto bridgeStatus = framePublisher_ ? framePublisher_->statusJson()
                                                          : Json::Value(Json::objectValue);
                if (!trim(config.commandTemplate).empty())
                {
                    int launchedPid = -1;
                    std::string launchError;
                    if (!launchExternalPublisher(config, *session, bridgeStatus, launchedPid, launchError))
                    {
                        failAndStop(launchError.empty()
                                        ? "Failed to launch external cloud publisher"
                                        : launchError);
                        break;
                    }

                    std::lock_guard lock(mutex_);
                    childPid_ = launchedPid;
                    if (stopRequested_)
                    {
                        stopExternalPublisherLocked();
                        running_ = false;
                        state_ = config.enabled ? "stopped" : "disabled";
                        lastMessage_ = "Cloud publisher stopped";
                        break;
                    }

                    state_ = "running";
                    lastMessage_ = "Cloud publishing launched via external publisher command";
                }
                else if (config.required)
                {
                    failAndStop(
                        "Cloud publishing is required but MEDIA_PUBLISH_COMMAND_TEMPLATE is not configured");
                    break;
                }

                launchAttempted = true;
            }
        }
        else if (config.frameStallTimeoutMs > 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastFrameProgressAt).count();
            if (elapsedMs >= config.frameStallTimeoutMs)
            {
                failAndStop(sawFrame
                                ? "Cloud publisher stopped receiving frames"
                                : "Cloud publisher did not receive the first frame before timeout");
                break;
            }
        }

        else if (!frameBridge_)
        {
            std::this_thread::sleep_for(waitInterval);
        }
    }
}

Json::Value CloudPublisher::statusJson() const
{
    const auto currentConfig = loadRuntimeConfig();
    std::lock_guard lock(mutex_);
    const auto& visibleConfig = running_ ? runtimeConfig_ : currentConfig;

    Json::Value json;
    json["enabled"] = visibleConfig.enabled;
    json["required"] = visibleConfig.required;
    json["state"] = state_;
    json["running"] = running_;
    json["device_id"] = visibleConfig.deviceId;
    json["backend_base_url"] = visibleConfig.backendBaseUrl;
    json["has_command_template"] = !trim(visibleConfig.commandTemplate).empty();
    json["shm_socket_path"] = visibleConfig.shmSocketPath;
    json["frame_stall_timeout_ms"] = visibleConfig.frameStallTimeoutMs;
    json["consumed_frame_count"] = Json::UInt64(consumedFrameCount_);
    json["last_frame_number"] = Json::UInt64(lastFrameNumber_);
    json["last_frame_timestamp_ns"] = Json::UInt64(lastFrameTimestampNs_);
    json["external_publisher_pid"] = childPid_;
    const auto nowNs = steadyNowNs();
    const auto frameAgeMs = lastFrameTimestampNs_ > 0 && nowNs >= lastFrameTimestampNs_
                                ? (nowNs - lastFrameTimestampNs_) / 1000000ULL
                                : std::numeric_limits<std::uint64_t>::max();
    if (lastFrameTimestampNs_ > 0)
        json["last_frame_age_ms"] = Json::UInt64(frameAgeMs);
    json["frames_progressing"] = running_ && consumedFrameCount_ > 0 && state_ != "failed" &&
                                  (visibleConfig.frameStallTimeoutMs <= 0 ||
                                   frameAgeMs < static_cast<std::uint64_t>(
                                       visibleConfig.frameStallTimeoutMs));

    if (!lastMessage_.empty())
        json["message"] = lastMessage_;
    if (!lastError_.empty())
        json["last_error"] = lastError_;

    if (session_.has_value())
    {
        json["session_id"] = session_->sessionId;
        json["stream_name"] = session_->streamName;
        json["whip_url"] = session_->whipUrl;
        json["view_url"] = session_->viewUrl;
        json["session_reused"] = session_->reused;
    }

    if (frameBridge_)
    {
        json["bridge_published_frame_count"] = Json::UInt64(frameBridge_->publishedFrameCount());
        json["bridge_acquired_frame_count"] = Json::UInt64(frameBridge_->acquiredFrameCount());
        json["bridge_dropped_frame_count"] = Json::UInt64(frameBridge_->droppedFrameCount());
    }

    if (framePublisher_)
        json["framebridge_export"] = framePublisher_->statusJson();

    return json;
}
