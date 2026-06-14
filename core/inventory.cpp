module;

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

module vaultium_core_inventory;

import vaultium_core_service_catalog;

namespace vaultium {
namespace {

// Single-quote a value for safe embedding in a /bin/sh command line.
[[nodiscard]] auto shellQuote(const std::string& value) -> std::string
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('\'');
    for (const char c : value) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result.push_back(c);
        }
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] auto splitLines(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::istringstream stream { text };
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto tokenize(const std::string& line) -> std::vector<std::string>
{
    std::vector<std::string> tokens;
    std::istringstream stream { line };
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

[[nodiscard]] auto displayNameFor(const std::string& id) -> std::string
{
    if (id == "nginx") return "Nginx";
    if (id == "apache") return "Apache";
    if (id == "caddy") return "Caddy";
    if (id == "systemd") return "systemd units";
    if (id == "docker") return "Docker";
    if (id == "mysql") return "MySQL / MariaDB";
    if (id == "postgresql") return "PostgreSQL";
    if (id == "redis") return "Redis";
    if (id == "mongodb") return "MongoDB";
    if (id == "xray") return "Xray";
    if (id == "fail2ban") return "Fail2Ban";
    if (id == "wireguard") return "WireGuard";
    if (id == "openvpn") return "OpenVPN";
    return id;
}

// Client/daemon binaries that indicate a service is installed even when its
// config lives somewhere non-standard.
[[nodiscard]] auto serviceBinaries(const std::string& id) -> std::vector<std::string>
{
    if (id == "nginx") return { "nginx" };
    if (id == "apache") return { "apache2", "apache2ctl", "httpd" };
    if (id == "caddy") return { "caddy" };
    if (id == "docker") return { "docker", "dockerd" };
    if (id == "mysql") return { "mariadbd", "mysqld", "mariadb", "mysql" };
    if (id == "postgresql") return { "postgres", "psql" };
    if (id == "redis") return { "redis-server" };
    if (id == "mongodb") return { "mongod" };
    if (id == "xray") return { "xray" };
    if (id == "fail2ban") return { "fail2ban-client", "fail2ban-server" };
    if (id == "wireguard") return { "wg", "wg-quick" };
    if (id == "openvpn") return { "openvpn" };
    return {};
}

} // namespace

auto LocalCommandRunner::run(const std::string& command) -> CommandResult
{
    int fds[2] {};
    if (::pipe(fds) == -1) {
        return { 255, {} };
    }

    const auto pid = ::fork();
    if (pid == -1) {
        ::close(fds[0]);
        ::close(fds[1]);
        return { 255, {} };
    }

    if (pid == 0) {
        ::dup2(fds[1], STDOUT_FILENO);

        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull != -1) {
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        ::close(fds[0]);
        ::close(fds[1]);

        ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(fds[1]);

    std::string output;
    std::array<char, 4096> buffer {};
    ssize_t count = 0;
    while ((count = ::read(fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 255;

    return { code, std::move(output) };
}

InventoryService::InventoryService(ICommandRunner& runner)
    : m_runner { runner }
{
}

auto InventoryService::disks() -> std::vector<DiskInfo>
{
    std::vector<DiskInfo> disks;

    const auto result = m_runner.run("df -Pk 2>/dev/null");
    if (result.exitCode != 0) {
        return disks;
    }

    const auto lines = splitLines(result.output);
    for (std::size_t i = 1; i < lines.size(); ++i) { // skip header
        const auto tokens = tokenize(lines[i]);
        if (tokens.size() < 6) {
            continue;
        }

        // POSIX df -P: filesystem, 1024-blocks, used, available, capacity, mount
        const auto toBytes = [](const std::string& kib) -> unsigned long long {
            return static_cast<unsigned long long>(std::strtoull(kib.c_str(), nullptr, 10)) * 1024ULL;
        };

        DiskInfo info;
        info.totalBytes = toBytes(tokens[1]);
        info.usedBytes = toBytes(tokens[2]);
        info.availBytes = toBytes(tokens[3]);
        info.path = tokens.back();

        // Skip pseudo / zero-size filesystems.
        if (info.totalBytes == 0) {
            continue;
        }
        disks.push_back(std::move(info));
    }

    return disks;
}

auto InventoryService::listDirectory(const std::string& path) -> std::vector<FsNode>
{
    std::vector<FsNode> nodes;

    std::string base = path;
    while (base.size() > 1 && base.back() == '/') {
        base.pop_back();
    }

    const auto result = m_runner.run("ls -1Ap -- " + shellQuote(path) + " 2>/dev/null");
    if (result.exitCode != 0) {
        return nodes;
    }

    for (const auto& raw : splitLines(result.output)) {
        if (raw.empty()) {
            continue;
        }

        FsNode node;
        node.isDir = raw.back() == '/';
        node.name = node.isDir ? raw.substr(0, raw.size() - 1) : raw;
        if (node.name.empty()) {
            continue;
        }
        node.path = (base == "/") ? "/" + node.name : base + "/" + node.name;
        nodes.push_back(std::move(node));
    }

    std::ranges::sort(nodes, [](const FsNode& a, const FsNode& b) {
        if (a.isDir != b.isDir) {
            return a.isDir > b.isDir; // directories first
        }
        return a.name < b.name;
    });

    return nodes;
}

auto InventoryService::detectServices() -> std::vector<ServiceInfo>
{
    // Probe every candidate path of every known service in one command, then
    // build the result from the set of paths that exist.
    std::vector<std::pair<std::string, std::vector<std::string>>> catalog;
    std::string probe = "for p in";

    for (const auto& id : knownServiceNames()) {
        const auto def = serviceDefinition(id);
        std::vector<std::string> paths;
        for (const auto& p : def.candidatePaths) {
            paths.push_back(p.string());
            probe += " " + shellQuote(p.string());
        }
        catalog.emplace_back(id, std::move(paths));
    }
    probe += "; do [ -e \"$p\" ] && echo \"path:$p\"; done 2>/dev/null; ";

    // Also probe for service binaries so a running service is detected even when
    // its config lives in a non-standard location (e.g. Xray under /usr/local).
    probe += "for b in";
    for (const auto& id : knownServiceNames()) {
        for (const auto& binary : serviceBinaries(id)) {
            probe += " " + shellQuote(binary);
        }
    }
    probe += "; do command -v \"$b\" >/dev/null 2>&1 && echo \"bin:$b\"; done 2>/dev/null";

    const auto result = m_runner.run(probe);

    std::vector<std::string> existingPaths;
    std::vector<std::string> existingBins;
    for (const auto& line : splitLines(result.output)) {
        if (line.starts_with("path:")) {
            existingPaths.push_back(line.substr(5));
        } else if (line.starts_with("bin:")) {
            existingBins.push_back(line.substr(4));
        }
    }
    const auto pathExists = [&existingPaths](const std::string& p) {
        return std::ranges::find(existingPaths, p) != existingPaths.end();
    };
    const auto binExists = [&existingBins](const std::string& b) {
        return std::ranges::find(existingBins, b) != existingBins.end();
    };

    std::vector<ServiceInfo> services;
    for (const auto& [id, paths] : catalog) {
        ServiceInfo info;
        info.id = id;
        info.displayName = displayNameFor(id);

        std::vector<std::string> present;
        for (const auto& p : paths) {
            if (pathExists(p)) {
                present.push_back(p);
            }
        }

        bool binaryFound = false;
        for (const auto& binary : serviceBinaries(id)) {
            if (binExists(binary)) {
                binaryFound = true;
                break;
            }
        }

        info.present = !present.empty() || binaryFound;
        if (!present.empty()) {
            info.assets.push_back(ServiceAsset { "Configuration", std::move(present) });
        }
        services.push_back(std::move(info));
    }

    return services;
}

auto InventoryService::pathSize(const std::string& path) -> unsigned long long
{
    // `du -sk` reports size in 1024-byte blocks; portable across GNU/BSD.
    const auto result = m_runner.run("du -sk -- " + shellQuote(path) + " 2>/dev/null");
    if (result.exitCode != 0) {
        return 0;
    }

    const auto tokens = tokenize(result.output);
    if (tokens.empty()) {
        return 0;
    }
    return static_cast<unsigned long long>(std::strtoull(tokens[0].c_str(), nullptr, 10)) * 1024ULL;
}

auto InventoryService::detectApplications(const std::vector<std::string>& extraRoots)
    -> std::vector<ApplicationInfo>
{
    std::vector<std::string> roots {
        "/var/www", "/var/www/html", "/srv/www", "/srv",
        "/usr/share/nginx/html", "/opt"
    };
    for (const auto& r : extraRoots) {
        roots.push_back(r);
    }

    // One pass: for each root and its immediate children, emit "type|path" for the
    // first matching application marker.
    std::string script = "for d in";
    for (const auto& r : roots) {
        script += " " + shellQuote(r);
    }
    script +=
        "; do [ -d \"$d\" ] || continue;"
        " for c in \"$d\" \"$d\"/*; do [ -d \"$c\" ] || continue;"
        " if [ -f \"$c/wp-config.php\" ]; then echo \"wordpress|$c\";"
        " elif [ -f \"$c/artisan\" ] && [ -f \"$c/composer.json\" ]; then echo \"laravel|$c\";"
        " elif [ -f \"$c/occ\" ]; then echo \"nextcloud|$c\";"
        " elif [ -f \"$c/docker-compose.yml\" ] || [ -f \"$c/compose.yaml\" ]; then echo \"docker|$c\";"
        " elif [ -f \"$c/package.json\" ]; then echo \"nodejs|$c\";"
        " elif [ -f \"$c/composer.json\" ] || [ -f \"$c/index.php\" ]; then echo \"php|$c\"; fi;"
        " done; done 2>/dev/null";

    const auto result = m_runner.run(script);

    const auto describe = [](const std::string& type) -> std::pair<std::string, std::pair<std::vector<std::string>, bool>> {
        if (type == "wordpress") return { "WordPress", { { "nginx", "apache" }, true } };
        if (type == "laravel")   return { "Laravel", { { "nginx", "apache" }, true } };
        if (type == "nextcloud") return { "Nextcloud", { { "nginx", "apache" }, true } };
        if (type == "docker")    return { "Docker stack", { { "docker" }, false } };
        if (type == "nodejs")    return { "Node.js application", { {}, false } };
        return { "PHP application", { { "nginx", "apache" }, true } };
    };

    std::vector<ApplicationInfo> apps;
    std::vector<std::string> seen;

    for (const auto& line : splitLines(result.output)) {
        const auto bar = line.find('|');
        if (bar == std::string::npos) {
            continue;
        }
        const auto type = line.substr(0, bar);
        const auto path = line.substr(bar + 1);

        if (std::ranges::find(seen, path) != seen.end()) {
            continue;
        }
        seen.push_back(path);

        const auto [name, meta] = describe(type);
        ApplicationInfo app;
        app.type = type;
        app.displayName = name;
        app.rootPath = path;
        app.paths = { path };
        app.services = meta.first;
        app.usesDatabase = meta.second;
        apps.push_back(std::move(app));
    }

    return apps;
}

namespace {
[[nodiscard]] auto isSafeIdentifier(const std::string& value) -> bool
{
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::ranges::all_of(value, [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '$';
    });
}
} // namespace

auto InventoryService::listDatabases(const std::string& engine) -> std::vector<std::string>
{
    std::vector<std::string> databases;
    if (engine != "mysql") {
        return databases; // PostgreSQL/SQLite enumeration handled elsewhere
    }

    const auto result = m_runner.run(
        "M=$(command -v mysql || command -v mariadb); "
        "[ -n \"$M\" ] && \"$M\" -N -B -e 'SHOW DATABASES' 2>/dev/null");

    for (const auto& line : splitLines(result.output)) {
        const auto db = line;
        if (db.empty() || db == "information_schema" || db == "performance_schema" || db == "sys") {
            continue;
        }
        databases.push_back(db);
    }
    return databases;
}

auto InventoryService::listTables(const std::string& engine, const std::string& database)
    -> std::vector<std::string>
{
    std::vector<std::string> tables;
    if (engine != "mysql" || !isSafeIdentifier(database)) {
        return tables;
    }

    const auto result = m_runner.run(
        "M=$(command -v mysql || command -v mariadb); "
        "[ -n \"$M\" ] && \"$M\" -N -B -e 'SHOW TABLES FROM `" + database + "`' 2>/dev/null");

    for (const auto& line : splitLines(result.output)) {
        if (!line.empty()) {
            tables.push_back(line);
        }
    }
    return tables;
}

auto InventoryService::detectDatabaseEngines() -> std::vector<std::string>
{
    std::vector<std::string> engines;
    bool haveMysql = false;

    // Detect by client binary OR running server (MariaDB ships `mariadb`, not always `mysql`).
    const auto result = m_runner.run(
        "for c in mysql mariadb mysqldump mariadb-dump psql sqlite3; do "
        "command -v \"$c\" >/dev/null 2>&1 && echo \"$c\"; done; "
        "(command -v mysqld >/dev/null 2>&1 || command -v mariadbd >/dev/null 2>&1 || pgrep -x mariadbd >/dev/null 2>&1 || pgrep -x mysqld >/dev/null 2>&1) && echo mysql-server");

    const auto add = [&engines](const std::string& e) {
        if (std::ranges::find(engines, e) == engines.end()) {
            engines.push_back(e);
        }
    };

    for (const auto& line : splitLines(result.output)) {
        if (line == "mysql" || line == "mariadb" || line == "mysqldump"
            || line == "mariadb-dump" || line == "mysql-server") {
            haveMysql = true;
        } else if (line == "psql") {
            add("postgresql");
        } else if (line == "sqlite3") {
            add("sqlite");
        }
    }
    if (haveMysql) {
        engines.insert(engines.begin(), "mysql"); // MySQL/MariaDB first
    }

    return engines;
}

} // namespace vaultium
