#pragma once

#include <drogon/HttpController.h>

class StreamingController : public drogon::HttpController<StreamingController>
{
public:
    void startStream(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void stopStream(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void startRecording(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void stopRecording(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void getStreamStatus(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void getRecordingStatus(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StreamingController::startStream, "/stream/start", drogon::Post);
    ADD_METHOD_TO(StreamingController::stopStream, "/stream/stop", drogon::Post);
    ADD_METHOD_TO(StreamingController::startRecording, "/record/start", drogon::Post);
    ADD_METHOD_TO(StreamingController::stopRecording, "/record/stop", drogon::Post);
    ADD_METHOD_TO(StreamingController::getStreamStatus, "/stream/status", drogon::Get);
    ADD_METHOD_TO(StreamingController::getRecordingStatus, "/record/status", drogon::Get);
    METHOD_LIST_END
};
