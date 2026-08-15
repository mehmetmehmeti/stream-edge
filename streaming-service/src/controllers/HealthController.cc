#include "controllers/HealthController.h"
#include "services/DatabaseService.h"
#include <drogon/drogon.h>

void HealthController::asyncHandleHttpRequest(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    (void)req;

    Json::Value json;
    json["service"] = "streaming-service";

    std::string errorMessage;
    const auto databaseHealthy =
        DatabaseService::instance().isHealthy(&errorMessage);

    json["status"] = databaseHealthy ? "ok" : "degraded";
    json["database"] = databaseHealthy ? "ok" : "error";
    if (!databaseHealthy)
        json["message"] = errorMessage;

    auto response = drogon::HttpResponse::newHttpJsonResponse(json);
    if (!databaseHealthy)
        response->setStatusCode(drogon::k500InternalServerError);

    callback(response);
}
