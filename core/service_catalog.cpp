module;

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

module vaultium_core_service_catalog;

namespace vaultium {
namespace {

[[nodiscard]] auto lower(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    return value;
}

struct CatalogEntry {
    std::string canonical;
    std::vector<std::string> aliases;       // accepted alternate names
    std::vector<std::string> configPaths;   // candidate config locations
};

// The single source of truth for known services and their config locations.
[[nodiscard]] auto catalog() -> const std::vector<CatalogEntry>&
{
    static const std::vector<CatalogEntry> entries {
        { "nginx",      {},                              { "/etc/nginx" } },
        { "apache",     { "apache2", "httpd" },          { "/etc/apache2", "/etc/httpd" } },
        { "caddy",      {},                              { "/etc/caddy" } },
        { "systemd",    {},                              { "/etc/systemd/system" } },
        { "docker",     {},                              { "/etc/docker" } },
        { "mysql",      { "mariadb" },                   { "/etc/mysql", "/etc/my.cnf", "/etc/my.cnf.d" } },
        { "postgresql", { "postgres", "psql" },          { "/etc/postgresql",
                                                          "/var/lib/pgsql/data/postgresql.conf",
                                                          "/var/lib/pgsql/data/pg_hba.conf" } },
        { "redis",      { "redis-server" },              { "/etc/redis", "/etc/redis/redis.conf", "/etc/redis.conf" } },
        { "mongodb",    { "mongo", "mongod" },           { "/etc/mongod.conf", "/etc/mongodb.conf", "/etc/mongodb" } },
        { "xray",       {},                              { "/usr/local/etc/xray", "/etc/xray" } },
        { "fail2ban",   {},                              { "/etc/fail2ban" } },
        { "wireguard",  { "wg" },                        { "/etc/wireguard" } },
        { "openvpn",    {},                              { "/etc/openvpn" } },
    };
    return entries;
}

[[nodiscard]] auto findEntry(const std::string& name) -> const CatalogEntry*
{
    const auto normalized = lower(name);
    for (const auto& entry : catalog()) {
        if (entry.canonical == normalized) {
            return &entry;
        }
        for (const auto& alias : entry.aliases) {
            if (alias == normalized) {
                return &entry;
            }
        }
    }
    return nullptr;
}

} // namespace

auto canonicalServiceName(const std::string& name) -> std::string
{
    const auto* entry = findEntry(name);
    return entry ? entry->canonical : std::string {};
}

auto isKnownService(const std::string& name) -> bool
{
    return findEntry(name) != nullptr;
}

auto serviceDefinition(const std::string& name) -> ServiceDefinition
{
    const auto* entry = findEntry(name);
    if (entry == nullptr) {
        throw std::runtime_error("Unknown service: " + name);
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(entry->configPaths.size());
    for (const auto& path : entry->configPaths) {
        paths.emplace_back(path);
    }
    return { entry->canonical, std::move(paths) };
}

auto knownServiceNames() -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(catalog().size());
    for (const auto& entry : catalog()) {
        names.push_back(entry.canonical);
    }
    return names;
}

} // namespace vaultium
