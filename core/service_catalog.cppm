module;

#include <filesystem>
#include <string>
#include <vector>

export module vaultium_core_service_catalog;

export namespace vaultium {

    /**
     * @brief Curated configuration locations for a known service.
     *
     * Paths are candidates: each is included in a service-config backup only if
     * it exists on disk. The catalog is the single source of truth shared by
     * config validation and the service-config backup source.
     */
    struct ServiceDefinition {
        std::string canonicalName;
        std::vector<std::filesystem::path> candidatePaths;
    };

    /**
     * @brief Normalizes a service alias to its canonical name.
     *
     * e.g. "mariadb" -> "mysql", "postgres" -> "postgresql", "httpd" -> "apache".
     * Returns an empty string for unknown services.
     */
    [[nodiscard]] auto canonicalServiceName(const std::string& name) -> std::string;

    /**
     * @brief Whether a service name (any accepted alias) is known.
     */
    [[nodiscard]] auto isKnownService(const std::string& name) -> bool;

    /**
     * @brief Resolves a service name to its curated configuration paths.
     *
     * @throws std::runtime_error for unknown services.
     */
    [[nodiscard]] auto serviceDefinition(const std::string& name) -> ServiceDefinition;

    /**
     * @brief All canonical service names known to the catalog.
     */
    [[nodiscard]] auto knownServiceNames() -> std::vector<std::string>;

} // namespace vaultium
