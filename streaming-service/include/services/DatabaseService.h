#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <QString>

namespace Orm
{
class DatabaseManager;
}

class DatabaseService {
public:
    static DatabaseService& instance();

    bool isHealthy(std::string* errorMessage = nullptr) const;
    QString threadConnection() const;

private:
    DatabaseService();

    std::shared_ptr<Orm::DatabaseManager> databaseManager_;
    mutable std::mutex mutex_;
    QString host_;
    QString database_;
    QString username_;
    QString password_;
    int port_{5432};
};
