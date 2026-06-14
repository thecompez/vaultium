module;

export module vaultium_remote_client;

import vaultium_core_backup_config;

export namespace vaultium::remote {

    /**
     * @brief Runs an agentless remote backup flow over libssh2.
     *
     * @param config Backup configuration.
     * @return Exit code.
     */
    auto runRemoteBackup(const BackupConfig& config) -> int;

} // namespace vaultium::remote