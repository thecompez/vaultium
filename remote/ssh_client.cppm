module;

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

export module vaultium_remote_ssh_client;

import vaultium_core_types;

export namespace vaultium::remote {

/**
 * @brief SSH connection configuration.
 */
    struct SshConnectionConfig {
        std::string host;
        std::uint16_t port { 22 };
        std::string username;

        RemoteAuthMethod authMethod { RemoteAuthMethod::Key };

        std::filesystem::path privateKeyPath;
        std::string privateKeyPassphrase {};

        std::string password {};

        // Host-key verification. When knownHostsFile is empty it defaults to
        // ~/.ssh/known_hosts. In strict mode an unknown or mismatched host key
        // aborts the connection; otherwise an unknown key is accepted with a
        // warning (a mismatch always aborts).
        std::filesystem::path knownHostsFile {};
        bool strictHostKey { true };

        std::chrono::seconds connectTimeout { 15 };
    };

/**
 * @brief Result of remote command execution.
 */
struct SshCommandResult {
    int exitCode {};
    std::string output;
    std::string errorOutput;
};

/**
 * @brief RAII wrapper around libssh2 SSH and SFTP operations.
 */
class SshClient final {
public:
    /**
     * @brief Constructs SSH client.
     *
     * @param config SSH connection config.
     */
    explicit SshClient(SshConnectionConfig config);

    SshClient(const SshClient&) = delete;
    auto operator=(const SshClient&) -> SshClient& = delete;

    SshClient(SshClient&&) = delete;
    auto operator=(SshClient&&) -> SshClient& = delete;

    /**
     * @brief Disconnects SSH session and releases libssh2 resources.
     */
    ~SshClient();

    /**
     * @brief Connects and authenticates using public key authentication.
     */
    auto connect() -> void;

    /**
     * @brief Executes command on remote server.
     *
     * @param command Remote shell command.
     * @return Command result.
     */
    [[nodiscard]] auto execute(const std::string& command) -> SshCommandResult;

    /**
     * @brief Downloads remote file to local path using SFTP.
     *
     * @param remotePath Remote file path.
     * @param localPath Local file path.
     */
    auto downloadFile(
        const std::filesystem::path& remotePath,
        const std::filesystem::path& localPath
    ) -> void;

    /**
     * @brief Uploads text content to a remote file using SFTP.
     *
     * @param remotePath Remote file path.
     * @param content Text content.
     * @param permissions File permissions.
     */
    auto uploadTextFile(
        const std::filesystem::path& remotePath,
        const std::string& content,
        long permissions = 0600
    ) -> void;

private:
    SshConnectionConfig m_config;

    int m_socket { -1 };
    void* m_session {};
    bool m_connected {};

    auto verifyHostKey() -> void;
    auto closeSession() noexcept -> void;
};

} // namespace vaultium::remote