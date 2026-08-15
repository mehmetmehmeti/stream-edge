#include "services/DatabaseService.h"

#include <QThread>
#include <cstdlib>
#include <orm/db.hpp>
#include <stdexcept>

namespace
{
QString requiredEnv(const char *name)
{
    const auto *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        throw std::runtime_error(std::string("Missing required environment variable: ") + name);

    return QString::fromUtf8(value);
}

int portFromEnv()
{
    const auto *value = std::getenv("DB_PORT");
    if (value == nullptr || *value == '\0')
        return 5432;

    bool ok = false;
    const auto port = QString::fromUtf8(value).toInt(&ok);
    if (!ok)
        throw std::runtime_error("Invalid DB_PORT value");

    return port;
}

QString connectionNameForCurrentThread()
{
    const auto threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("streaming_service_%1").arg(threadId);
}
} // namespace

DatabaseService& DatabaseService::instance()
{
    static DatabaseService service;
    return service;
}

DatabaseService::DatabaseService()
{
    host_ = requiredEnv("DB_HOST");
    port_ = portFromEnv();
    database_ = requiredEnv("DB_NAME");
    username_ = requiredEnv("DB_USER");
    password_ = requiredEnv("DB_PASSWORD");
    databaseManager_ = Orm::DB::create();
}

bool DatabaseService::isHealthy(std::string* errorMessage) const
{
    try
    {
        const auto connectionName = threadConnection();
        const auto result = Orm::DB::scalar(QStringLiteral("select 1"), {},
                                            connectionName);

        if (!result.isValid() || result.toInt() != 1)
        {
            if (errorMessage)
                *errorMessage = "TinyORM query returned an unexpected result";
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (errorMessage)
            *errorMessage = e.what();
        return false;
    }
}

QString DatabaseService::threadConnection() const
{
    if (!databaseManager_)
        throw std::runtime_error("Database manager is not configured");

    const auto name = connectionNameForCurrentThread();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!databaseManager_->containsConnection(name))
    {
        databaseManager_->addConnection({
            {QStringLiteral("driver"), QStringLiteral("QPSQL")},
            {QStringLiteral("application_name"), QStringLiteral("streaming-service")},
            {QStringLiteral("host"), host_},
            {QStringLiteral("port"), port_},
            {QStringLiteral("database"), database_},
            {QStringLiteral("search_path"), QStringLiteral("public")},
            {QStringLiteral("username"), username_},
            {QStringLiteral("password"), password_},
            {QStringLiteral("charset"), QStringLiteral("utf8")},
            {QStringLiteral("timezone"), QStringLiteral("UTC")},
            {QStringLiteral("prefix"), QStringLiteral("")},
            {QStringLiteral("prefix_indexes"), false},
        }, name);
    }

    databaseManager_->connectEagerly(name);
    return name;
}
