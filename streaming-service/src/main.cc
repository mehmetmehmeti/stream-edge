#include <QCoreApplication>
#include <drogon/drogon.h>

#include "services/CommandSyncService.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

void trim(std::string& value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
}

std::size_t loadDotEnvIfPresent(const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
        return 0;

    std::size_t loadedCount = 0;
    std::string line;
    while (std::getline(input, line))
    {
        trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0)
            continue;

        auto key = line.substr(0, separator);
        auto value = line.substr(separator + 1);
        trim(key);
        trim(value);
        if (key.empty() || std::getenv(key.c_str()) != nullptr)
            continue;

        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
        {
            value = value.substr(1, value.size() - 2);
        }

        ::setenv(key.c_str(), value.c_str(), 0);
        ++loadedCount;
    }

    return loadedCount;
}

std::optional<fs::path> findProjectFile(const fs::path& relativePath,
                                        const char* executablePath)
{
    std::vector<fs::path> searchRoots;
    searchRoots.push_back(fs::current_path());

    if (executablePath != nullptr && *executablePath != '\0')
    {
        std::error_code error;
        auto executable = fs::absolute(executablePath, error);
        if (!error)
            searchRoots.push_back(executable.parent_path());
    }

    std::set<fs::path> visited;
    for (auto root : searchRoots)
    {
        while (!root.empty() && visited.insert(root).second)
        {
            for (const auto& candidate : {
                     root / relativePath,
                     root / "streaming-service" / relativePath,
                 })
            {
                std::error_code error;
                if (fs::is_regular_file(candidate, error) && !error)
                    return candidate;
            }

            const auto parent = root.parent_path();
            if (parent == root)
                break;
            root = parent;
        }
    }

    return std::nullopt;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication qtApp(argc, argv);

    if (const auto envPath = findProjectFile(".env", argv[0]))
    {
        const auto loadedEnvCount = loadDotEnvIfPresent(envPath->string());
        std::cerr << "Loaded " << loadedEnvCount
                  << " streaming-service .env entries from " << envPath->string()
                  << std::endl;
    }
    else
    {
        std::cerr << "No streaming-service .env file found; using process environment"
                  << std::endl;
    }

    const auto configPath = findProjectFile("config/config.json", argv[0]);
    if (!configPath)
    {
        std::cerr << "Unable to locate streaming-service config/config.json" << std::endl;
        return 1;
    }

    drogon::app().loadConfigFile(configPath->string());
    CommandSyncService::instance().start();
    drogon::app().run();
    CommandSyncService::instance().stop();
    return 0;
}
