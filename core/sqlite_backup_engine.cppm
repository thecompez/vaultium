module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>
#include <iostream>
#include <string>

export module vaultium_core_sqlite_backup_engine;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_database_backup_engine;

export namespace vaultium {

    /**
     * @brief SQLite backup engine.
     */
    class SqliteBackupEngine final : public IDatabaseBackupEngine {
    public:
        [[nodiscard]] auto name() const -> std::string override;

        [[nodiscard]] auto createBackup(
            const BackupConfig& config,
            const std::filesystem::path& temporaryFile
        ) const -> BackupArtifact override;

    private:
        auto copySqliteFiles(
            const BackupConfig& config,
            const std::filesystem::path& temporaryDirectory
        ) const -> void;
    };

} // namespace vaultium