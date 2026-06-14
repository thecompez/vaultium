module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

export module vaultium_core_postgresql_backup_engine;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_database_backup_engine;

export namespace vaultium {

    /**
     * @brief PostgreSQL backup engine.
     */
    class PostgreSqlBackupEngine final : public IDatabaseBackupEngine {
    public:
        [[nodiscard]] auto name() const -> std::string override;

        [[nodiscard]] auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact override;

    private:
        [[nodiscard]] auto buildPgDumpArguments(
            const BackupConfig& config,
            const std::string& database
        ) const -> std::vector<std::string>;

        [[nodiscard]] auto buildPgDumpAllArguments(const BackupConfig& config) const -> std::vector<std::string>;

        [[nodiscard]] auto readPassword(const std::filesystem::path& path) const -> std::string;
    };

} // namespace vaultium