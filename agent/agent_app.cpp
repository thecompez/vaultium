module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

module vaultium_agent_app;

import vaultium_core_logger;
import vaultium_core_backup_manager;

namespace vaultium::agent {

    auto runAgent(const BackupConfig& config) -> int
    {
        Logger::info("Vaultium Agent started.");
        Logger::warning("Agent API server is scaffolded but not implemented yet.");
        Logger::info("Running local backup loop for now.");

        BackupManager manager { config };
        manager.runLoop();

        return 0;
    }

} // namespace vaultium::agent