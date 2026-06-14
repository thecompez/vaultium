module;

#include <memory>
#include <stdexcept>

module vaultium_core_backup_source;

import vaultium_core_types;
import vaultium_core_database_backup_source;
import vaultium_core_filesystem_backup_source;
import vaultium_core_service_config_backup_source;

namespace vaultium {

auto createBackupSource(const BackupConfig& config) -> std::unique_ptr<IBackupSource>
{
    switch (config.sourceType) {
    case SourceType::Database:
        return std::make_unique<DatabaseBackupSource>();

    case SourceType::Filesystem:
        return std::make_unique<FilesystemBackupSource>();

    case SourceType::ServiceConfig:
        return std::make_unique<ServiceConfigBackupSource>();
    }

    throw std::runtime_error("Unsupported backup source type.");
}

} // namespace vaultium
