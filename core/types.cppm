module;

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

export module vaultium_core_types;

export namespace vaultium {

    /**
     * @brief Supported database backup engines.
     */
    enum class BackupEngineType {
        MySql,
        PostgreSql,
        Sqlite
    };

    /**
     * @brief Database selection mode.
     */
    enum class DatabaseMode {
        All,
        Selected
    };

    /**
     * @brief Top-level backup source category.
     *
     * Selects which IBackupSource implementation handles a job. Database is the
     * original engine family; Filesystem archives arbitrary paths; ServiceConfig
     * captures service configuration trees (Nginx, systemd, ...).
     */
    enum class SourceType {
        Database,
        Filesystem,
        ServiceConfig
    };

    /**
     * @brief Backup execution mode.
     */
    enum class ExecutionMode {
        Local,
        RemoteSsh,
        Agent
    };

    /**
     * @brief Remote SSH authentication method.
     */
    enum class RemoteAuthMethod {
        Key,
        Password
    };

    /**
     * @brief Optional descriptive metadata a source attaches to an artifact.
     *
     * Sources that resolve a dynamic set of inputs (e.g. service-config) record
     * what was included and what was skipped. The manager serializes this into a
     * ".meta.json" sidecar alongside the checksum.
     */
    struct ArtifactMetadata {
        // Non-empty when the source wants a metadata sidecar written.
        bool present {};

        // Source-specific detail, e.g. the service list "nginx,systemd".
        std::string detail {};

        std::vector<std::string> includedPaths {};
        std::vector<std::string> skippedPaths {};
    };

    /**
     * @brief Represents a generated backup file.
     */
    struct BackupArtifact {
        std::filesystem::path path;
        std::uintmax_t size {};
        ArtifactMetadata metadata {};
    };

    /**
     * @brief Represents an external process result.
     */
    struct ProcessResult {
        int exitCode {};
    };

} // namespace vaultium