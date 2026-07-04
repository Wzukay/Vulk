#include "logger.h"

void GameLogger::AddLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back(message);
    if (logLines.size() > MAX_LOGS) {
        logLines.erase(logLines.begin());
    }
}

std::vector<std::string> GameLogger::GetLogs() {
    std::lock_guard<std::mutex> lock(logMutex);
    return logLines;
}
        
