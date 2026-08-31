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
    std::filesystem::path knownHostsFile {};
    bool strictHostKey { true };
    std::chrono::seconds connectTimeout { 15 };
    std::chrono::seconds commandTimeout { 600 };
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
    explicit SshClient(SshConnectionConfig config);

    SshClient(const SshClient&) = delete;
    auto operator=(const SshClient&) -> SshClient& = delete;
    SshClient(SshClient&&) = delete;
    auto operator=(SshClient&&) -> SshClient& = delete;
    ~SshClient();

    /**
     * @brief Connects, verifies the host key, and authenticates.
     */
    auto connect() -> void;

    /**
     * @brief Executes a remote command within commandTimeout.
     */
    [[nodiscard]] auto execute(const std::string& command) -> SshCommandResult;

    /**
     * @brief Downloads a remote file with an inactivity timeout.
     */
    auto downloadFile(
        const std::filesystem::path& remotePath,
        const std::filesystem::path& localPath
    ) -> void;

    /**
     * @brief Uploads text content with an inactivity timeout.
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
