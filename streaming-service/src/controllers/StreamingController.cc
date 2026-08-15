#include "controllers/StreamingController.h"

#include "services/CommandSyncService.h"
#include "services/StreamingService.h"

#include <drogon/drogon.h>

namespace
{
drogon::HttpResponsePtr jsonResponse(const Json::Value& payload,
                                     const drogon::HttpStatusCode status = drogon::k200OK)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(status);
    return response;
}

std::string optionalCaseIdFromRequest(const drogon::HttpRequestPtr& req)
{
    auto json = req->getJsonObject();
    if (json && (*json)["case_id"].isString())
        return (*json)["case_id"].asString();
    return {};
}

bool metadataManagedByClient(const drogon::HttpRequestPtr& req)
{
    const auto json = req->getJsonObject();
    return json && (*json)["metadata_managed_by_client"].asBool();
}
} // namespace

void StreamingController::startStream(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto& streamingService = StreamingService::instance();
    Json::Value json;
    json["service"] = "streaming-service";
    const auto caseId = optionalCaseIdFromRequest(req);

    if (!streamingService.isHoloscanConfigured())
    {
        json["status"] = "error";
        json["message"] = "Holoscan pipeline config is missing";
        callback(jsonResponse(json, drogon::k500InternalServerError));
        return;
    }

    std::string sourceError;
    if (!streamingService.isStreamingSourceConfigured(&sourceError))
    {
        json["status"] = "error";
        json["message"] = sourceError;
        callback(jsonResponse(json, drogon::k400BadRequest));
        return;
    }

    std::string sourceSignalError;
    if (!streamingService.hasStreamingSourceSignal(&sourceSignalError))
    {
        json["status"] = "error";
        json["message"] = sourceSignalError.empty()
                              ? "Streaming source signal is unavailable"
                              : sourceSignalError;
        callback(jsonResponse(json, drogon::k400BadRequest));
        return;
    }

    std::string message;
    const auto started = streamingService.startStream(&message);
    if (!started)
    {
        json["status"] = "error";
        json["message"] = message;
        CommandSyncService::instance().recordLocalAction(
            caseId, "start", "stream", "failed", {}, message, "error", "idle");
        callback(jsonResponse(json, drogon::k500InternalServerError));
        return;
    }

    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["message"] = message;
    CommandSyncService::instance().recordLocalAction(
        caseId,
        "start",
        "stream",
        "completed",
        message,
        {},
        "live",
        streamingService.isRecording() ? "recording" : "idle");
    callback(jsonResponse(json));
}

void StreamingController::stopStream(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto& streamingService = StreamingService::instance();
    Json::Value json;
    json["service"] = "streaming-service";
    const auto caseId = optionalCaseIdFromRequest(req);

    std::string message;
    const auto stopped = streamingService.stopStream(&message);
    if (!stopped)
    {
        json["status"] = "error";
        json["message"] = message;
        CommandSyncService::instance().recordLocalAction(
            caseId, "stop", "stream", "failed", {}, message, "error", "idle");
        callback(jsonResponse(json, drogon::k500InternalServerError));
        return;
    }

    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["message"] = message;
    CommandSyncService::instance().recordLocalAction(
        caseId,
        "stop",
        "stream",
        "completed",
        message,
        {},
        "offline",
        streamingService.isRecording() ? "recording" : "idle");
    callback(jsonResponse(json));
}

void StreamingController::getStreamStatus(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    (void)req;

    auto& streamingService = StreamingService::instance();
    Json::Value json;
    json["service"] = "streaming-service";
    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["holoscanConfigured"] = streamingService.isHoloscanConfigured();
    const auto publisherStatus = streamingService.publisherStatus();

    std::string sourceError;
    const auto sourceConfigured = streamingService.isStreamingSourceConfigured(&sourceError);
    json["sourceConfigured"] = sourceConfigured;
    if (!sourceConfigured)
        json["sourceError"] = sourceError;

    std::string sourceSignalError;
    const auto sourceSignal = streamingService.isStreaming() && publisherStatus["enabled"].asBool()
                                  ? publisherStatus["frames_progressing"].asBool()
                                  : streamingService.hasStreamingSourceSignal(&sourceSignalError);
    json["sourceSignal"] = sourceSignal;
    if (!sourceSignal)
    {
        json["sourceSignalError"] = sourceSignalError.empty() && streamingService.isStreaming()
                                        ? "Frame production is stalled"
                                        : sourceSignalError;
    }

    const auto lastError = streamingService.lastError();
    if (!lastError.empty())
        json["lastError"] = lastError;

    json["recordingStatus"] = streamingService.recordingStatus();
    json["publisher"] = publisherStatus;
    if (publisherStatus["enabled"].asBool() &&
        (publisherStatus["state"].asString() == "failed" || !sourceSignal))
    {
        json["status"] = "error";
    }
    json["commandSync"] = CommandSyncService::instance().statusJson();

    callback(jsonResponse(json));
}

void StreamingController::startRecording(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto& streamingService = StreamingService::instance();
    const auto caseId = optionalCaseIdFromRequest(req);
    const auto clientManagesMetadata = metadataManagedByClient(req);

    Json::Value json;
    json["service"] = "streaming-service";

    std::string message;
    const auto started = streamingService.startRecording(caseId, &message);
    if (!started)
    {
        json["status"] = "error";
        json["message"] = message;
        if (!clientManagesMetadata)
        {
            CommandSyncService::instance().recordLocalAction(
                caseId,
                "start",
                "record",
                "failed",
                {},
                message,
                streamingService.isStreaming() ? "live" : "error",
                "error");
        }
        callback(jsonResponse(json, drogon::k500InternalServerError));
        return;
    }

    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["recordingStatus"] = streamingService.recordingStatus();
    json["message"] = message;
    if (!clientManagesMetadata)
    {
        CommandSyncService::instance().recordLocalAction(
            caseId,
            "start",
            "record",
            "completed",
            message,
            {},
            streamingService.isStreaming() ? "live" : "offline",
            "recording",
            json["recordingStatus"]);
    }
    callback(jsonResponse(json));
}

void StreamingController::stopRecording(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto& streamingService = StreamingService::instance();
    const auto caseId = optionalCaseIdFromRequest(req);
    const auto clientManagesMetadata = metadataManagedByClient(req);

    Json::Value json;
    json["service"] = "streaming-service";

    std::string message;
    const auto stopped = streamingService.stopRecording(&message, caseId);
    if (!stopped)
    {
        json["status"] = "error";
        json["message"] = message;
        if (!clientManagesMetadata)
        {
            CommandSyncService::instance().recordLocalAction(
                caseId,
                "stop",
                "record",
                "failed",
                {},
                message,
                streamingService.isStreaming() ? "live" : "error",
                "error");
        }
        callback(jsonResponse(json, drogon::k500InternalServerError));
        return;
    }

    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["recordingStatus"] = streamingService.recordingStatus();
    json["message"] = message;
    if (!clientManagesMetadata)
    {
        CommandSyncService::instance().recordLocalAction(
            caseId,
            "stop",
            "record",
            "completed",
            message,
            {},
            streamingService.isStreaming() ? "live" : "offline",
            "idle",
            json["recordingStatus"]);
    }
    callback(jsonResponse(json));
}

void StreamingController::getRecordingStatus(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    (void)req;

    auto& streamingService = StreamingService::instance();
    Json::Value json;
    json["service"] = "streaming-service";
    json["status"] = "ok";
    json["streaming"] = streamingService.isStreaming();
    json["recording"] = streamingService.isRecording();
    json["recordingStatus"] = streamingService.recordingStatus();
    json["commandSync"] = CommandSyncService::instance().statusJson();
    json["publisher"] = streamingService.publisherStatus();
    callback(jsonResponse(json));
}
