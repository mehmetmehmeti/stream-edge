#include "services/CommandSyncService.h"

#include "services/DatabaseService.h"
#include "services/StreamingService.h"

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketClient.h>
#include <drogon/utils/Utilities.h>
#include <QDateTime>
#include <orm/db.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <json/json.h>
#include <sstream>
#include <thread>
#include <sys/wait.h>

namespace
{
QString connectionName()
{
    return DatabaseService::instance().threadConnection();
}

QString outboxCommandsTableSql()
{
    return QStringLiteral(
        "create table if not exists outbox_commands ("
        "id text primary key, case_id text null, device_id text not null, command text not null, "
        "action text not null, status text not null, result_message text null, error_message text null, "
        "session_status text null, recording_status text null, recording_metadata jsonb null, "
        "processed_at timestamptz null, "
        "created_at timestamptz not null default now(), updated_at timestamptz not null default now())");
}

std::string trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string getEnvOrDefault(const char* name, const std::string& defaultValue)
{
    const auto* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty())
        return defaultValue;
    return value;
}

std::string deriveWebSocketUrl(const std::string &backendBaseUrl)
{
    if (backendBaseUrl.rfind("https://", 0) == 0)
        return "wss://" + backendBaseUrl.substr(8);
    if (backendBaseUrl.rfind("http://", 0) == 0)
        return "ws://" + backendBaseUrl.substr(7);
    if (backendBaseUrl.rfind("ws://", 0) == 0 || backendBaseUrl.rfind("wss://", 0) == 0)
        return backendBaseUrl;
    return {};
}

struct WebSocketEndpoint
{
    std::string baseUrl;
    std::string path{"/sync"};
};

WebSocketEndpoint parseWebSocketEndpoint(const std::string &value)
{
    WebSocketEndpoint endpoint;
    if (value.empty())
        return endpoint;

    const auto schemePos = value.find("://");
    if (schemePos == std::string::npos)
    {
        endpoint.baseUrl = value;
        return endpoint;
    }

    const auto hostStart = schemePos + 3;
    const auto pathStart = value.find('/', hostStart);
    if (pathStart == std::string::npos)
    {
        endpoint.baseUrl = value;
        return endpoint;
    }

    endpoint.baseUrl = value.substr(0, pathStart);
    endpoint.path = value.substr(pathStart);
    if (endpoint.path.empty())
        endpoint.path = "/sync";
    return endpoint;
}

std::string ensureSyncTokenInPath(const std::string &path, const std::string &token)
{
    auto resolvedPath = path.empty() ? std::string("/sync") : path;
    if (token.empty() || resolvedPath.find("token=") != std::string::npos)
        return resolvedPath;

    resolvedPath += (resolvedPath.find('?') == std::string::npos) ? '?' : '&';
    resolvedPath += "token=" + drogon::utils::urlEncode(token);
    return resolvedPath;
}

bool getEnvAsBool(const char* name, bool defaultValue)
{
    const auto raw = trim(getEnvOrDefault(name, defaultValue ? "1" : "0"));
    return raw == "1" || raw == "true" || raw == "TRUE" || raw == "yes";
}

int getEnvAsInt(const char* name, int defaultValue)
{
    const auto raw = trim(getEnvOrDefault(name, std::to_string(defaultValue)));
    try
    {
        return raw.empty() ? defaultValue : std::stoi(raw);
    }
    catch (const std::exception&)
    {
        return defaultValue;
    }
}

std::string shellQuote(const std::string& value)
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

std::string stringField(const Json::Value& json, const char* name)
{
    return json[name].isString() ? json[name].asString() : std::string{};
}

std::string utcNowIsoString()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

Json::Int64 durationSecondsBetween(const std::string& recordedAt,
                                   const std::string& endedAt)
{
    const auto started = QDateTime::fromString(
        QString::fromStdString(recordedAt), Qt::ISODate);
    const auto ended = QDateTime::fromString(
        QString::fromStdString(endedAt), Qt::ISODate);

    if (!started.isValid() || !ended.isValid())
        return 0;

    return static_cast<Json::Int64>(std::max<qint64>(0, started.secsTo(ended)));
}

void attachRecordingMetadata(Json::Value& target,
                             const Json::Value& recordingStatus,
                             bool stopped)
{
    const auto outputPath = stringField(recordingStatus, "output_path");
    if (!outputPath.empty())
    {
        target["local_path"] = outputPath;
        target["output_path"] = outputPath;
    }

    target["content_type"] = "video/mp4";
    const auto recordedAt = stringField(recordingStatus, "recorded_at");
    if (!recordedAt.empty())
        target["recorded_at"] = recordedAt;

    if (stopped)
    {
        const auto endedAt = stringField(recordingStatus, "ended_at");
        const auto resolvedEndedAt = endedAt.empty() ? utcNowIsoString() : endedAt;
        target["ended_at"] = resolvedEndedAt;

        Json::Int64 duration = 0;
        if (recordingStatus["duration"].isIntegral())
            duration = recordingStatus["duration"].asInt64();
        if (duration <= 0 && !recordedAt.empty())
            duration = durationSecondsBetween(recordedAt, resolvedEndedAt);
        target["duration"] = duration;
    }
    else
    {
        target["recorded_at"] = recordedAt.empty() ? utcNowIsoString() : recordedAt;
    }
}

void mergeJsonObject(Json::Value& target, const Json::Value& source)
{
    if (!source.isObject())
        return;

    for (const auto& name : source.getMemberNames())
        target[name] = source[name];
}

void copyField(Json::Value& target,
               const Json::Value& source,
               const char* field)
{
    target[field] = source.isMember(field) ? source[field] : Json::Value();
}

std::string deviceStatusSignature(const Json::Value& payload)
{
    Json::Value signature(Json::objectValue);
    for (const auto* field : {"holoscan_configured", "streaming", "recording",
                              "source_configured", "source_signal", "source_error",
                              "source_signal_error", "last_error"})
    {
        copyField(signature, payload, field);
    }

    const auto& recording = payload["recording_status"];
    if (recording.isObject())
    {
        Json::Value recordingSignature(Json::objectValue);
        for (const auto* field : {"active", "finalized", "path", "last_error"})
            copyField(recordingSignature, recording, field);
        signature["recording_status"] = recordingSignature;
    }

    const auto& publisher = payload["publisher_status"];
    if (publisher.isObject())
    {
        Json::Value publisherSignature(Json::objectValue);
        for (const auto* field : {"enabled", "required", "state", "running",
                                  "message", "last_error", "session_id",
                                  "stream_name"})
        {
            copyField(publisherSignature, publisher, field);
        }
        signature["publisher_status"] = publisherSignature;
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, signature);
}
} // namespace

CommandSyncService& CommandSyncService::instance()
{
    static CommandSyncService service;
    return service;
}

CommandSyncService::~CommandSyncService()
{
    stop();
}

void CommandSyncService::start()
{
    if (running_)
        return;

    runtimeConfig_ = loadRuntimeConfig();
    if (!runtimeConfig_.enabled)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = "disabled";
        lastMessage_ = "Command sync is disabled";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        websocketState_ = runtimeConfig_.syncWebSocketUrl.empty() ? "disabled" : "connecting";
        lastWebSocketError_.clear();
    }

    try
    {
        ensureOutboxTable();
    }
    catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        runtimeConfig_.enabled = false;
        state_ = "disabled";
        websocketState_ = "disabled";
        lastError_ = e.what();
        lastMessage_ = "Command sync is disabled because the local database is unavailable";
        return;
    }

    stopRequested_ = false;
    running_ = true;
    workerThread_ = std::thread([this]() { workerLoop(); });
}

void CommandSyncService::stop()
{
    stopRequested_ = true;
    wakeCondition_.notify_all();
    if (workerThread_.joinable())
        workerThread_.join();
    running_ = false;
}

Json::Value CommandSyncService::statusJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Json::Value json(Json::objectValue);
    json["enabled"] = runtimeConfig_.enabled;
    json["running"] = running_.load();
    json["state"] = state_;
    json["device_id"] = runtimeConfig_.deviceId;
    json["backend_base_url"] = runtimeConfig_.backendBaseUrl;
    json["sync_websocket_url"] = runtimeConfig_.syncWebSocketUrl;
    json["processed_command_count"] = Json::UInt64(processedCommandCount_);
    json["synced_local_action_count"] = Json::UInt64(syncedLocalActionCount_);
    json["synced_device_status_count"] = Json::UInt64(syncedDeviceStatusCount_);
    json["device_status_interval_ms"] = runtimeConfig_.deviceStatusIntervalMs;
    json["device_status_keepalive_ms"] = runtimeConfig_.deviceStatusKeepaliveMs;
    json["websocket_enabled"] = !runtimeConfig_.syncWebSocketUrl.empty();
    json["websocket_connected"] = websocketConnected_.load();
    json["websocket_state"] = websocketState_;
    if (!lastError_.empty())
        json["last_error"] = lastError_;
    if (!lastMessage_.empty())
        json["message"] = lastMessage_;
    if (!lastWebSocketError_.empty())
        json["last_websocket_error"] = lastWebSocketError_;
    return json;
}

void CommandSyncService::recordLocalAction(const std::string& caseId,
                                           const std::string& command,
                                           const std::string& action,
                                           const std::string& status,
                                           const std::string& resultMessage,
                                           const std::string& errorMessage,
                                           const std::string& sessionStatus,
                                           const std::string& recordingStatus,
                                           const Json::Value& recordingMetadata)
{
    ensureOutboxTable();

    Json::Value metadata = recordingMetadata.isObject()
                               ? recordingMetadata
                               : Json::Value(Json::objectValue);
    if (action == "record" && status == "completed")
    {
        if (!metadata["content_type"].isString())
            metadata["content_type"] = "video/mp4";
        if (command == "start" && !metadata["recorded_at"].isString())
            metadata["recorded_at"] = utcNowIsoString();
        if (command == "stop" && !metadata["ended_at"].isString())
            metadata["ended_at"] = utcNowIsoString();
    }

    QVariant metadataValue;
    if (!metadata.empty())
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        metadataValue = QString::fromStdString(Json::writeString(writer, metadata));
    }

    Orm::DB::statement(
        "insert into outbox_commands "
        "(id, case_id, device_id, command, action, status, result_message, error_message, "
        "session_status, recording_status, recording_metadata, created_at, updated_at) "
        "values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?::jsonb, now(), now())",
        {
            QString::fromStdString(drogon::utils::getUuid()),
            caseId.empty() ? QVariant() : QVariant(QString::fromStdString(caseId)),
            QString::fromStdString(runtimeConfig_.deviceId),
            QString::fromStdString(command),
            QString::fromStdString(action),
            QString::fromStdString(status),
            resultMessage.empty() ? QVariant() : QVariant(QString::fromStdString(resultMessage)),
            errorMessage.empty() ? QVariant() : QVariant(QString::fromStdString(errorMessage)),
            sessionStatus.empty() ? QVariant() : QVariant(QString::fromStdString(sessionStatus)),
            recordingStatus.empty() ? QVariant() : QVariant(QString::fromStdString(recordingStatus)),
            metadataValue,
        },
        connectionName());
}

CommandSyncService::RuntimeConfig CommandSyncService::loadRuntimeConfig() const
{
    RuntimeConfig config;
    config.enabled = getEnvAsBool("EDGE_COMMAND_SYNC_ENABLED", true);
    config.backendBaseUrl = trim(getEnvOrDefault("BACKEND_API_BASE_URL", "http://127.0.0.1:8080"));
    const auto rawSyncUrl = trim(getEnvOrDefault(
        "CLOUD_SYNC_WS_URL",
        getEnvOrDefault("EDGE_SYNC_WS_URL", deriveWebSocketUrl(config.backendBaseUrl))));
    const auto endpoint = parseWebSocketEndpoint(rawSyncUrl);
    config.syncWebSocketUrl = endpoint.baseUrl;
    config.syncWebSocketPath = endpoint.path;
    config.internalToken = trim(getEnvOrDefault("EDGE_INTERNAL_API_TOKEN", ""));
    config.syncToken = trim(getEnvOrDefault("EDGE_SYNC_TOKEN", ""));
    config.deviceId = trim(getEnvOrDefault("EDGE_DEVICE_ID", ""));
    config.pollIntervalMs = getEnvAsInt("EDGE_COMMAND_SYNC_POLL_INTERVAL_MS", 500);
    config.deviceStatusIntervalMs =
        getEnvAsInt("EDGE_DEVICE_STATUS_INTERVAL_MS", 5000);
    config.deviceStatusKeepaliveMs =
        getEnvAsInt("EDGE_DEVICE_STATUS_KEEPALIVE_MS", 25000);
    config.websocketReconnectMs = getEnvAsInt("EDGE_SYNC_WS_RECONNECT_MS", 3000);
    if (config.backendBaseUrl.empty() || config.internalToken.empty() || config.deviceId.empty())
        config.enabled = false;
    if (config.pollIntervalMs < 250)
        config.pollIntervalMs = 250;
    if (config.deviceStatusIntervalMs < 1000)
        config.deviceStatusIntervalMs = 1000;
    if (config.deviceStatusKeepaliveMs < config.deviceStatusIntervalMs)
        config.deviceStatusKeepaliveMs = config.deviceStatusIntervalMs;
    if (config.websocketReconnectMs < 500)
        config.websocketReconnectMs = 500;
    return config;
}

void CommandSyncService::ensureOutboxTable()
{
    DatabaseService::instance();
    Orm::DB::unprepared(outboxCommandsTableSql(), connectionName());
    Orm::DB::unprepared(
        QStringLiteral("alter table outbox_commands add column if not exists recording_metadata jsonb null"),
        connectionName());
}

void CommandSyncService::ensureWebSocketConnection()
{
    if (runtimeConfig_.syncWebSocketUrl.empty() || websocketConnected_.load())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (lastWebSocketConnectAttempt_.time_since_epoch().count() != 0 &&
        now - lastWebSocketConnectAttempt_ <
            std::chrono::milliseconds(runtimeConfig_.websocketReconnectMs))
    {
        return;
    }

    lastWebSocketConnectAttempt_ = now;
    resetWebSocketClient();

    websocketClient_ = drogon::WebSocketClient::newWebSocketClient(
        runtimeConfig_.syncWebSocketUrl);
    websocketClient_->setMessageHandler(
        [this](std::string &&message,
               const drogon::WebSocketClientPtr &,
               const drogon::WebSocketMessageType &type) {
            handleWebSocketMessage(std::move(message), type);
        });
    websocketClient_->setConnectionClosedHandler(
        [this](const drogon::WebSocketClientPtr &) {
            websocketConnected_ = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                websocketState_ = "disconnected";
            }
        });

    auto request = drogon::HttpRequest::newHttpRequest();
    request->setPath(ensureSyncTokenInPath(runtimeConfig_.syncWebSocketPath,
                                           runtimeConfig_.syncToken));
    websocketClient_->connectToServer(
        request,
        [this](drogon::ReqResult result,
               const drogon::HttpResponsePtr &,
               const drogon::WebSocketClientPtr &client) {
            if (result != drogon::ReqResult::Ok)
            {
                websocketConnected_ = false;
                std::lock_guard<std::mutex> lock(mutex_);
                websocketState_ = "error";
                lastWebSocketError_ = "Failed to connect websocket";
                return;
            }

            websocketConnected_ = true;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                websocketState_ = "connected";
                lastWebSocketError_.clear();
            }
        });
}

void CommandSyncService::resetWebSocketClient()
{
    if (websocketClient_)
    {
        websocketClient_->stop();
        websocketClient_.reset();
    }
}

void CommandSyncService::handleWebSocketMessage(
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if (type != drogon::WebSocketMessageType::Text)
        return;

    Json::CharReaderBuilder reader;
    Json::Value payload;
    std::string parseErrors;
    std::istringstream input(message);
    if (!Json::parseFromStream(reader, input, &payload, &parseErrors))
        return;

    const auto messageType = stringField(payload, "type");
    if (messageType == "ack")
        return;

    const auto eventId = stringField(payload, "event_id");
    const auto entityType = stringField(payload, "entity_type");
    const auto source = stringField(payload, "source");
    if (entityType != "livestream_command" || source != "cloud" || eventId.empty())
        return;

    const auto data = payload["data"];
    if (!data.isObject())
    {
        acknowledgeSyncEvent(eventId, false, "Missing livestream command payload");
        return;
    }

    const auto targetDeviceId = stringField(data, "device_id");
    if (!targetDeviceId.empty() && targetDeviceId != runtimeConfig_.deviceId)
        return;

    immediateFetchRequested_ = true;
    wakeCondition_.notify_all();
    acknowledgeSyncEvent(eventId, true, "Livestream command notification received");
}

void CommandSyncService::acknowledgeSyncEvent(const std::string &eventId,
                                              bool applied,
                                              const std::string &message)
{
    Json::Value ack(Json::objectValue);
    ack["type"] = "ack";
    ack["event_id"] = eventId;
    ack["status"] = applied ? "applied" : "failed";
    if (!message.empty())
        ack["message"] = message;
    sendWebSocketJson(ack);
}

void CommandSyncService::sendWebSocketJson(const Json::Value &payload)
{
    if (!websocketClient_ || !websocketConnected_.load())
        return;

    if (auto connection = websocketClient_->getConnection(); connection && connection->connected())
        connection->send(Json::writeString(Json::StreamWriterBuilder(), payload));
}

bool CommandSyncService::runShellCommandWithOutput(const std::string& command,
                                                   std::string& output,
                                                   int& exitCode)
{
    output.clear();
    FILE* pipe = popen(command.c_str(), "r");
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

bool CommandSyncService::fetchNextCommand(Json::Value& command, std::string& error)
{
    std::ostringstream shell;
    shell << "curl -fsS "
          << shellQuote(runtimeConfig_.backendBaseUrl + "/internal/livestream-commands/next?device_id=" +
                        runtimeConfig_.deviceId)
          << " -H " << shellQuote("Authorization: Bearer " + runtimeConfig_.internalToken);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(shell.str(), output, exitCode) || exitCode != 0)
    {
        error = "Failed to fetch next livestream command";
        return false;
    }

    Json::CharReaderBuilder reader;
    std::string parseErrors;
    std::istringstream input(output);
    if (!Json::parseFromStream(reader, input, &command, &parseErrors))
    {
        error = "Invalid JSON from next livestream command endpoint";
        return false;
    }
    return true;
}

bool CommandSyncService::claimCommand(const std::string& commandId, std::string& error)
{
    Json::Value body;
    body["device_id"] = runtimeConfig_.deviceId;
    body["claimed_by"] = runtimeConfig_.deviceId;
    Json::StreamWriterBuilder writer;
    const auto payload = Json::writeString(writer, body);

    std::ostringstream shell;
    shell << "curl -fsS -X POST "
          << shellQuote(runtimeConfig_.backendBaseUrl + "/internal/livestream-commands/" +
                        commandId + "/claim")
          << " -H 'Content-Type: application/json' "
          << " -H " << shellQuote("Authorization: Bearer " + runtimeConfig_.internalToken)
          << " --data " << shellQuote(payload);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(shell.str(), output, exitCode) || exitCode != 0)
    {
        error = "Failed to claim livestream command";
        return false;
    }
    return true;
}

bool CommandSyncService::updateCommandStatus(const std::string& commandId,
                                             const Json::Value& payload,
                                             std::string& error)
{
    Json::Value body = payload;
    body["device_id"] = runtimeConfig_.deviceId;
    Json::StreamWriterBuilder writer;
    const auto serialized = Json::writeString(writer, body);

    std::ostringstream shell;
    shell << "curl -fsS -X POST "
          << shellQuote(runtimeConfig_.backendBaseUrl + "/internal/livestream-commands/" +
                        commandId + "/status")
          << " -H 'Content-Type: application/json' "
          << " -H " << shellQuote("Authorization: Bearer " + runtimeConfig_.internalToken)
          << " --data " << shellQuote(serialized);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(shell.str(), output, exitCode) || exitCode != 0)
    {
        error = "Failed to update livestream command status";
        return false;
    }
    return true;
}

bool CommandSyncService::sendLocalAction(const Json::Value& payload, std::string& error)
{
    Json::Value body = payload;
    body["device_id"] = runtimeConfig_.deviceId;
    Json::StreamWriterBuilder writer;
    const auto serialized = Json::writeString(writer, body);

    std::ostringstream shell;
    shell << "curl -fsS -X POST "
          << shellQuote(runtimeConfig_.backendBaseUrl + "/internal/livestream/local-actions")
          << " -H 'Content-Type: application/json' "
          << " -H " << shellQuote("Authorization: Bearer " + runtimeConfig_.internalToken)
          << " --data " << shellQuote(serialized);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(shell.str(), output, exitCode) || exitCode != 0)
    {
        error = "Failed to sync local outbox action";
        return false;
    }
    return true;
}

bool CommandSyncService::sendDeviceStatus(const Json::Value& payload, std::string& error)
{
    Json::Value body = payload;
    body["device_id"] = runtimeConfig_.deviceId;
    Json::StreamWriterBuilder writer;
    const auto serialized = Json::writeString(writer, body);

    std::ostringstream shell;
    shell << "curl -fsS -X POST "
          << shellQuote(runtimeConfig_.backendBaseUrl + "/internal/device-status")
          << " -H 'Content-Type: application/json' "
          << " -H " << shellQuote("Authorization: Bearer " + runtimeConfig_.internalToken)
          << " --data " << shellQuote(serialized);

    std::string output;
    int exitCode = -1;
    if (!runShellCommandWithOutput(shell.str(), output, exitCode) || exitCode != 0)
    {
        error = "Failed to sync device status";
        return false;
    }
    return true;
}

void CommandSyncService::syncDeviceStatus()
{
    const auto now = std::chrono::steady_clock::now();
    if (lastDeviceStatusReport_.time_since_epoch().count() != 0 &&
        now - lastDeviceStatusReport_ <
            std::chrono::milliseconds(runtimeConfig_.deviceStatusIntervalMs))
    {
        return;
    }

    lastDeviceStatusReport_ = now;

    auto& streamingService = StreamingService::instance();
    Json::Value payload(Json::objectValue);
    payload["holoscan_configured"] = streamingService.isHoloscanConfigured();
    payload["streaming"] = streamingService.isStreaming();
    payload["recording"] = streamingService.isRecording();

    std::string sourceError;
    const auto sourceConfigured =
        streamingService.isStreamingSourceConfigured(&sourceError);
    payload["source_configured"] = sourceConfigured;
    if (!sourceError.empty())
        payload["source_error"] = sourceError;

    std::string sourceSignalError;
    const auto sourceSignal = streamingService.isStreaming()
                                  ? true
                                  : streamingService.hasStreamingSourceSignal(&sourceSignalError);
    payload["source_signal"] = sourceSignal;
    if (!sourceSignalError.empty())
        payload["source_signal_error"] = sourceSignalError;

    const auto lastError = streamingService.lastError();
    if (!lastError.empty())
        payload["last_error"] = lastError;

    payload["recording_status"] = streamingService.recordingStatus();
    payload["publisher_status"] = streamingService.publisherStatus();

    const auto signature = deviceStatusSignature(payload);
    const auto neverSent =
        lastDeviceStatusSend_.time_since_epoch().count() == 0;
    const auto changed = signature != lastDeviceStatusSignature_;
    const auto keepaliveDue =
        !neverSent &&
        now - lastDeviceStatusSend_ >=
            std::chrono::milliseconds(runtimeConfig_.deviceStatusKeepaliveMs);

    if (!changed && !keepaliveDue)
        return;

    std::string error;
    if (!sendDeviceStatus(payload, error))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = error;
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    lastDeviceStatusSignature_ = signature;
    lastDeviceStatusSend_ = now;
    ++syncedDeviceStatusCount_;
}

void CommandSyncService::syncPendingLocalActions()
{
    auto query = Orm::DB::select(
        QStringLiteral("select id, case_id, command, action, status, result_message, error_message, "
                       "session_status, recording_status, recording_metadata from outbox_commands "
                       "where processed_at is null order by created_at asc limit 10"),
        {},
        connectionName());

    while (query.next())
    {
        Json::Value payload(Json::objectValue);
        payload["case_id"] = query.value("case_id").toString().toStdString();
        payload["command"] = query.value("command").toString().toStdString();
        payload["action"] = query.value("action").toString().toStdString();
        payload["status"] = query.value("status").toString().toStdString();
        payload["result_message"] =
            query.value("result_message").toString().toStdString();
        payload["error_message"] =
            query.value("error_message").toString().toStdString();
        payload["session_status"] =
            query.value("session_status").toString().toStdString();
        payload["recording_status"] =
            query.value("recording_status").toString().toStdString();
        const auto metadataText =
            query.value("recording_metadata").toString().toStdString();
        if (!metadataText.empty())
        {
            Json::CharReaderBuilder reader;
            std::string parseErrors;
            Json::Value metadata(Json::objectValue);
            std::istringstream input(metadataText);
            if (Json::parseFromStream(reader, input, &metadata, &parseErrors))
                mergeJsonObject(payload, metadata);
        }

        std::string error;
        if (!sendLocalAction(payload, error))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastError_ = error;
            break;
        }

        Orm::DB::statement(
            "update outbox_commands set processed_at = now(), updated_at = now() where id = ?",
            {query.value("id")},
            connectionName());

        std::lock_guard<std::mutex> lock(mutex_);
        ++syncedLocalActionCount_;
    }
}

void CommandSyncService::workerLoop()
{
    while (!stopRequested_)
    {
        try
        {
            ensureWebSocketConnection();
            syncDeviceStatus();
            syncPendingLocalActions();

            Json::Value command;
            std::string error;
            if (!fetchNextCommand(command, error))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = "error";
                lastError_ = error;
            }
            else if (command["available"].asBool())
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    lastError_.clear();
                }

                const auto commandId = stringField(command, "command_id");
                if (!claimCommand(commandId, error))
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    state_ = "error";
                    lastError_ = error;
                }
                else
                {
                    Json::Value processing;
                    processing["status"] = "processing";
                    updateCommandStatus(commandId, processing, error);

                    const auto caseId = stringField(command, "case_id");
                    const auto commandName = stringField(command, "command");
                    const auto action = stringField(command, "action");
                    std::string resultMessage;
                    bool success = false;
                    std::string sessionStatus;
                    std::string recordingStatus;
                    Json::Value recordingMetadata(Json::objectValue);
                    bool includeRecordingMetadata = false;
                    bool recordingStopped = false;

                    auto& streamingService = StreamingService::instance();
                    if (action == "stream" && commandName == "start")
                    {
                        success = streamingService.startStream(&resultMessage);
                        sessionStatus = success ? "live" : "error";
                    }
                    else if (action == "stream" && commandName == "stop")
                    {
                        success = streamingService.stopStream(&resultMessage);
                        sessionStatus = success ? "offline" : "error";
                    }
                    else if (action == "record" && commandName == "start")
                    {
                        success = streamingService.startRecording(caseId, &resultMessage);
                        sessionStatus = streamingService.isStreaming() ? "live" : "";
                        recordingStatus = success ? "recording" : "error";
                        recordingMetadata = streamingService.recordingStatus();
                        includeRecordingMetadata = success;
                    }
                    else if (action == "record" && commandName == "stop")
                    {
                        success = streamingService.stopRecording(&resultMessage, caseId);
                        sessionStatus = streamingService.isStreaming() ? "live" : "offline";
                        recordingStatus = success ? "idle" : "error";
                        recordingMetadata = streamingService.recordingStatus();
                        includeRecordingMetadata = success;
                        recordingStopped = true;
                    }
                    else
                    {
                        resultMessage = "Unsupported command/action";
                    }

                    Json::Value completed;
                    completed["status"] = success ? "completed" : "failed";
                    completed["result_message"] = resultMessage;
                    if (!success)
                        completed["error_message"] = resultMessage;
                    if (!sessionStatus.empty())
                        completed["session_status"] = sessionStatus;
                    if (!recordingStatus.empty())
                        completed["recording_status"] = recordingStatus;
                    if (includeRecordingMetadata)
                        attachRecordingMetadata(completed, recordingMetadata, recordingStopped);
                    bool finalStatusUpdated = false;
                    for (int attempt = 0; attempt < 3 && !finalStatusUpdated; ++attempt)
                    {
                        finalStatusUpdated = updateCommandStatus(commandId, completed, error);
                        if (!finalStatusUpdated)
                            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    }
                    lastDeviceStatusReport_ = {};
                    syncDeviceStatus();

                    std::lock_guard<std::mutex> lock(mutex_);
                    if (finalStatusUpdated)
                        ++processedCommandCount_;
                    state_ = success && finalStatusUpdated ? "idle" : "error";
                    lastMessage_ = resultMessage;
                    if (!success)
                        lastError_ = resultMessage;
                    else if (!finalStatusUpdated)
                        lastError_ = error.empty()
                                         ? "Failed to report final livestream command status"
                                         : error;
                }
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = "idle";
                lastError_.clear();
            }
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = "error";
            lastError_ = e.what();
        }

        std::unique_lock<std::mutex> lock(mutex_);
        wakeCondition_.wait_for(
            lock,
            std::chrono::milliseconds(runtimeConfig_.pollIntervalMs),
            [this]() { return stopRequested_.load() || immediateFetchRequested_.load(); });
        immediateFetchRequested_ = false;
    }

    resetWebSocketClient();
}
