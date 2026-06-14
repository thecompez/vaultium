module;

export module vaultium_remote_provisioner;

import vaultium_core_backup_config;
import vaultium_remote_ssh_client;

export namespace vaultium::remote {

    /**
     * @brief Tests remote SSH connectivity and server tool availability.
     *
     * @param config Backup configuration.
     * @return Exit code.
     */
    auto runRemoteTest(const BackupConfig& config) -> int;

    /**
     * @brief Provisions remote server-side Vaultium requirements.
     *
     * @param config Backup configuration.
     * @return Exit code.
     */
    auto runRemoteProvision(const BackupConfig& config) -> int;

    /**
     * @brief Provisions remote server using an already connected SSH client.
     *
     * @param ssh Connected SSH client.
     * @param config Backup configuration.
     */
    auto provisionRemoteServer(SshClient& ssh, const BackupConfig& config) -> void;

} // namespace vaultium::remote