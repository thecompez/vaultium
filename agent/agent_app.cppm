module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

export module vaultium_agent_app;

import vaultium_core_backup_config;

export namespace vaultium::agent {

    /**
     * @brief Runs Vaultium agent mode.
     *
     * @param config Backup configuration.
     * @return Exit code.
     */
    auto runAgent(const BackupConfig& config) -> int;

} // namespace vaultium::agent