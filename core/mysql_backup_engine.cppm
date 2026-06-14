module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

export module vaultium_core_mysql_backup_engine;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_database_backup_engine;

export namespace vaultium {

    /**
     * @brief MySQL and MariaDB backup engine.
     */
    class MySqlBackupEngine final : public IDatabaseBackupEngine {
    public:
        [[nodiscard]] auto name() const -> std::string override;

        [[nodiscard]] auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact override;

    private:
        [[nodiscard]] auto buildArguments(const BackupConfig& config) const -> std::vector<std::string>;
    };

} // namespace vaultium