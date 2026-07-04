#pragma once

#include <string>
#include <vector>
#include <mutex>

class GameLogger {
private:
    std::vector<std::string> logLines;
    std::mutex logMutex;
    const size_t MAX_LOGS = 12;

public:
    void AddLog(const std::string& message);
    std::vector<std::string> GetLogs();
};

extern GameLogger debugLog;