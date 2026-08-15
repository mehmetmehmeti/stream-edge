#pragma once

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace drogon
{
class WebSocketClient;
using WebSocketClientPtr = std::shared_ptr<WebSocketClient>;
enum class WebSocketMessageType;
}

class CommandSyncService
{
public:
    static CommandSyncService& instance();
    ~CommandSyncService();

    void start();
    void stop();
    Json::Value statusJson() const;
    void recordLocalAction(const std::string& caseId,
                           const std::string& command,
                           const std::string& action,
                           const std::string& status,
                           const std::string& resultMessage = {},
                           const std::string& errorMessage = {},
                           const std::string& sessionStatus = {},
                           const std::string& recordingStatus = {},
                           const Json::Value& recordingMetadata = Json::Value());

private:
    struct RuntimeConfig
    {
        bool enabled{false};
        std::string backendBaseUrl;
        std::string syncWebSocketUrl;
        std::string syncWebSocketPath{"/sync"};
        std::string internalToken;
        std::string syncToken;
        std::string deviceId;
        int pollIntervalMs{500};
        int deviceStatusIntervalMs{5000};
        int deviceStatusKeepaliveMs{25000};
        int websocketReconnectMs{3000};
    };

    CommandSyncService() = default;
    CommandSyncService(const CommandSyncService&) = delete;
    CommandSyncService& operator=(const CommandSyncService&) = delete;

    void workerLoop();
    RuntimeConfig loadRuntimeConfig() const;
    void ensureOutboxTable();
    void syncPendingLocalActions();
    void ensureWebSocketConnection();
    void resetWebSocketClient();
    void handleWebSocketMessage(std::string &&message,
                                const drogon::WebSocketMessageType &type);
    void acknowledgeSyncEvent(const std::string &eventId,
                              bool applied,
                              const std::string &message = {});
    void sendWebSocketJson(const Json::Value &payload);
    bool fetchNextCommand(Json::Value& command, std::string& error);
    bool claimCommand(const std::string& commandId, std::string& error);
    bool updateCommandStatus(const std::string& commandId,
                             const Json::Value& payload,
                             std::string& error);
    bool sendLocalAction(const Json::Value& payload, std::string& error);
    bool sendDeviceStatus(const Json::Value& payload, std::string& error);
    void syncDeviceStatus();
    static bool runShellCommandWithOutput(const std::string& command,
                                          std::string& output,
                                          int& exitCode);

    mutable std::mutex mutex_;
    RuntimeConfig runtimeConfig_{};
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> websocketConnected_{false};
    std::atomic<bool> immediateFetchRequested_{false};
    std::string state_{"disabled"};
    std::string lastError_;
    std::string lastMessage_;
    std::string websocketState_{"disabled"};
    std::string lastWebSocketError_;
    std::uint64_t processedCommandCount_{0};
    std::uint64_t syncedLocalActionCount_{0};
    std::uint64_t syncedDeviceStatusCount_{0};
    drogon::WebSocketClientPtr websocketClient_;
    std::condition_variable wakeCondition_;
    std::chrono::steady_clock::time_point lastWebSocketConnectAttempt_{};
    std::chrono::steady_clock::time_point lastDeviceStatusReport_{};
    std::chrono::steady_clock::time_point lastDeviceStatusSend_{};
    std::string lastDeviceStatusSignature_;
};
