module;

#include <filesystem>
#include <string>

export module vaultium_core_sqlite_backup_engine;

import vaultium_core_types;
import vaultium_core_backup_config;
import vaultium_core_database_backup_engine;

export namespace vaultium {

/**
 * @brief SQLite backup engine using the SQLite online backup command.
 *
 * Each configured database is snapshotted through the sqlite3 CLI before the
 * resulting snapshots are archived. This avoids raw live-file copies that can
 * be inconsistent while WAL or transactions are active.
 */
class SqliteBackupEngine final : public IDatabaseBackupEngine {
public:
    [[nodiscard]] auto name() const -> std::string override;

    [[nodiscard]] auto createBackup(
        const BackupConfig& config,
        const std::filesystem::path& temporaryFile
    ) const -> BackupArtifact override;

private:
    auto snapshotSqliteFiles(
        const BackupConfig& config,
        const std::filesystem::path& temporaryDirectory
    ) const -> void;
};

} // namespace vaultium
