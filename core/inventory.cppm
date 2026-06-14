module;

#include <string>
#include <vector>

export module vaultium_core_inventory;

export namespace vaultium {

/**
 * @brief Result of running a single read-only probe command.
 */
struct CommandResult {
    int exitCode {};
    std::string output;
};

/**
 * @brief Abstraction over "run a shell command and capture stdout".
 *
 * Implemented by LocalCommandRunner (this machine) and SshCommandRunner (a remote
 * server). The InventoryService is written against this interface so the same
 * discovery logic works locally and over SSH.
 */
class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;
    [[nodiscard]] virtual auto run(const std::string& command) -> CommandResult = 0;
};

/**
 * @brief Runs commands on the local machine via `/bin/sh -c`, capturing stdout.
 */
class LocalCommandRunner final : public ICommandRunner {
public:
    [[nodiscard]] auto run(const std::string& command) -> CommandResult override;
};

// -- Inventory data model -----------------------------------------------------

struct DiskInfo {
    std::string path;          // mount point
    unsigned long long totalBytes {};
    unsigned long long usedBytes {};
    unsigned long long availBytes {};
};

struct FsNode {
    std::string name;
    std::string path;
    bool isDir {};
};

struct ServiceAsset {
    std::string label;                  // e.g. "Configuration"
    std::vector<std::string> paths;     // existing paths only
};

struct ServiceInfo {
    std::string id;                     // canonical id, e.g. "nginx"
    std::string displayName;            // e.g. "Nginx"
    bool present {};
    std::vector<ServiceAsset> assets;
};

/**
 * @brief A real application/workload detected on the machine (the higher-level
 * view non-technical users think in: "WordPress", not "/var/www/html").
 */
struct ApplicationInfo {
    std::string type;                   // wordpress | laravel | nextcloud | docker | nodejs | php
    std::string displayName;            // "WordPress"
    std::string rootPath;               // where it was found
    std::vector<std::string> paths;     // paths to back up for this app
    std::vector<std::string> services;  // related service ids (e.g. "nginx")
    bool usesDatabase {};               // whether the app typically has a database
};

/**
 * @brief Read-only discovery of a machine's disks, files, services and databases.
 *
 * Every method issues injection-safe, read-only commands through the injected
 * runner. Nothing here mutates the target.
 */
class InventoryService {
public:
    explicit InventoryService(ICommandRunner& runner);

    /// Mounted filesystems with capacity (via `df -Pk`).
    [[nodiscard]] auto disks() -> std::vector<DiskInfo>;

    /// Immediate children of a directory (via `ls -1Ap`), directories first.
    [[nodiscard]] auto listDirectory(const std::string& path) -> std::vector<FsNode>;

    /// Known services detected by the presence of their config paths.
    [[nodiscard]] auto detectServices() -> std::vector<ServiceInfo>;

    /// Database engine client binaries available on the target (e.g. "mysql").
    [[nodiscard]] auto detectDatabaseEngines() -> std::vector<std::string>;

    /// Databases on the target for an engine (uses the server's default auth,
    /// e.g. socket auth for root). System databases are filtered out.
    [[nodiscard]] auto listDatabases(const std::string& engine) -> std::vector<std::string>;

    /// Tables in a database for an engine.
    [[nodiscard]] auto listTables(const std::string& engine, const std::string& database)
        -> std::vector<std::string>;

    /// Recursively measured size of a path in bytes (via `du`). 0 if unknown.
    [[nodiscard]] auto pathSize(const std::string& path) -> unsigned long long;

    /// Higher-level applications detected under common web roots (plus any extra
    /// roots, which the tests use). Read-only marker-file detection.
    [[nodiscard]] auto detectApplications(const std::vector<std::string>& extraRoots = {})
        -> std::vector<ApplicationInfo>;

private:
    ICommandRunner& m_runner;
};

} // namespace vaultium
