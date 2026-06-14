module;

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <ostream>
#include <vector>
#include <iostream>
#include <string>
#include <utility>

module vaultium_cli_app;

import vaultium_core_types;
import vaultium_core_backup_manager;
import vaultium_core_backup_source;
import vaultium_core_inventory;
import vaultium_core_scheduler;
import vaultium_remote_provisioner;
import vaultium_core_backup_config;
import vaultium_core_logger;
import vaultium_remote_client;
import vaultium_remote_ssh_client;
import vaultium_remote_ssh_command_runner;
import vaultium_agent_app;

namespace vaultium::cli {
namespace {

struct CliOptions {
    std::filesystem::path configPath { "/etc/vaultium/vaultium.conf" };
    bool runOnce {};
    bool runLoop {};
    bool runAgent {};
    bool runRemote {};
    bool runRemoteTest {};
    bool runRemoteProvision {};
    bool runRestore {};
    bool runVerify {};
    bool runInspect {};
    bool showHelp {};

    std::filesystem::path archivePath {};
    RestoreOptions restoreOptions {};

    std::string inspectWhat { "dir" }; // dir | disks | services | dbengines | apps | size
    std::string inspectPath { "/" };
    bool inspectSession {};

    // Scheduler: `schedule <action> [flags]`.
    bool runSchedule {};
    std::string scheduleAction;        // list | save | remove | set-enabled | run | repair | status
    std::string schedId;
    std::string schedName;
    std::string schedType { "daily" }; // once | daily | weekly | monthly | cron
    std::string schedTime { "02:00" }; // HH:MM
    std::string schedDow { "1" };      // 0-6 (weekly)
    std::string schedDom { "1" };      // 1-31 (monthly)
    std::string schedOnce;             // "YYYY-MM-DD HH:MM" (once)
    std::string schedCron;             // raw cron (cron type)
    std::string schedConfig;           // backup config the schedule runs
    std::string schedBackupType;       // filesystem | database | service-config | mixed
    std::string schedEnabled { "true" };
    std::string schedScope { "user" };  // user | system
};

// -- Minimal JSON output for the `inspect` command ---------------------------

[[nodiscard]] auto jsonEscape(const std::string& value) -> std::string
{
    std::string out;
    out.reserve(value.size() + 2);
    for (const char c : value) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

// Emits exactly one line of JSON describing the requested probe. The "type"
// field lets a session client correlate responses.
auto emitInventoryJson(ICommandRunner& runner, const std::string& what, const std::string& path) -> void
{
    InventoryService inventory { runner };

    if (what == "disks") {
        const auto disks = inventory.disks();
        std::cout << "{\"type\":\"disks\",\"disks\":[";
        for (std::size_t i = 0; i < disks.size(); ++i) {
            const auto& d = disks[i];
            std::cout << (i ? "," : "")
                << "{\"path\":\"" << jsonEscape(d.path) << "\",\"totalBytes\":" << d.totalBytes
                << ",\"usedBytes\":" << d.usedBytes << ",\"availBytes\":" << d.availBytes << "}";
        }
        std::cout << "]}\n";
    } else if (what == "services") {
        const auto services = inventory.detectServices();
        std::cout << "{\"type\":\"services\",\"services\":[";
        for (std::size_t i = 0; i < services.size(); ++i) {
            const auto& s = services[i];
            std::cout << (i ? "," : "")
                << "{\"id\":\"" << jsonEscape(s.id) << "\",\"name\":\"" << jsonEscape(s.displayName)
                << "\",\"present\":" << (s.present ? "true" : "false") << ",\"assets\":[";
            for (std::size_t a = 0; a < s.assets.size(); ++a) {
                const auto& asset = s.assets[a];
                std::cout << (a ? "," : "") << "{\"label\":\"" << jsonEscape(asset.label) << "\",\"paths\":[";
                for (std::size_t p = 0; p < asset.paths.size(); ++p) {
                    std::cout << (p ? "," : "") << "\"" << jsonEscape(asset.paths[p]) << "\"";
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
    } else if (what == "dbengines") {
        const auto engines = inventory.detectDatabaseEngines();
        std::cout << "{\"type\":\"dbengines\",\"engines\":[";
        for (std::size_t i = 0; i < engines.size(); ++i) {
            std::cout << (i ? "," : "") << "\"" << jsonEscape(engines[i]) << "\"";
        }
        std::cout << "]}\n";
    } else if (what == "apps") {
        const auto apps = inventory.detectApplications();
        std::cout << "{\"type\":\"apps\",\"apps\":[";
        for (std::size_t i = 0; i < apps.size(); ++i) {
            const auto& a = apps[i];
            std::cout << (i ? "," : "")
                << "{\"app\":\"" << jsonEscape(a.type) << "\",\"name\":\"" << jsonEscape(a.displayName)
                << "\",\"root\":\"" << jsonEscape(a.rootPath) << "\",\"usesDatabase\":"
                << (a.usesDatabase ? "true" : "false") << ",\"paths\":[";
            for (std::size_t p = 0; p < a.paths.size(); ++p) {
                std::cout << (p ? "," : "") << "\"" << jsonEscape(a.paths[p]) << "\"";
            }
            std::cout << "],\"services\":[";
            for (std::size_t s = 0; s < a.services.size(); ++s) {
                std::cout << (s ? "," : "") << "\"" << jsonEscape(a.services[s]) << "\"";
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
    } else if (what == "databases") {
        const auto dbs = inventory.listDatabases(path); // path = engine
        std::cout << "{\"type\":\"databases\",\"engine\":\"" << jsonEscape(path) << "\",\"databases\":[";
        for (std::size_t i = 0; i < dbs.size(); ++i) {
            std::cout << (i ? "," : "") << "\"" << jsonEscape(dbs[i]) << "\"";
        }
        std::cout << "]}\n";
    } else if (what == "tables") {
        // path = "<engine> <database>"
        const auto sp = path.find(' ');
        const std::string engine = sp == std::string::npos ? path : path.substr(0, sp);
        const std::string database = sp == std::string::npos ? std::string {} : path.substr(sp + 1);
        const auto tbls = inventory.listTables(engine, database);
        std::cout << "{\"type\":\"tables\",\"db\":\"" << jsonEscape(database) << "\",\"tables\":[";
        for (std::size_t i = 0; i < tbls.size(); ++i) {
            std::cout << (i ? "," : "") << "\"" << jsonEscape(tbls[i]) << "\"";
        }
        std::cout << "]}\n";
    } else if (what == "size") {
        const auto bytes = inventory.pathSize(path);
        std::cout << "{\"type\":\"size\",\"path\":\"" << jsonEscape(path) << "\",\"bytes\":" << bytes << "}\n";
    } else { // dir
        const auto nodes = inventory.listDirectory(path);
        std::cout << "{\"type\":\"dir\",\"path\":\"" << jsonEscape(path) << "\",\"entries\":[";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            std::cout << (i ? "," : "")
                << "{\"name\":\"" << jsonEscape(n.name) << "\",\"path\":\"" << jsonEscape(n.path)
                << "\",\"isDir\":" << (n.isDir ? "true" : "false") << "}";
        }
        std::cout << "]}\n";
    }
    std::cout.flush();
}

// Reads "command [arg]" lines from stdin and answers each with one JSON line,
// keeping the (possibly remote) connection open for the whole session.
auto runInspectSession(ICommandRunner& runner) -> void
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const auto space = line.find(' ');
        const auto command = line.substr(0, space);
        const auto arg = space == std::string::npos ? std::string {} : line.substr(space + 1);
        emitInventoryJson(runner, command, arg.empty() ? "/" : arg);
    }
}

auto runInspect(const BackupConfig& config, const CliOptions& options) -> int
{
    const auto dispatch = [&](ICommandRunner& runner) {
        if (options.inspectSession) {
            runInspectSession(runner);
        } else {
            emitInventoryJson(runner, options.inspectWhat, options.inspectPath);
        }
    };

    if (config.executionMode == ExecutionMode::RemoteSsh) {
        remote::SshConnectionConfig sshConfig {
            .host = config.remoteHost,
            .port = config.remotePort,
            .username = config.remoteUser,
            .authMethod = config.remoteAuthMethod,
            .privateKeyPath = config.remoteIdentityFile,
            .privateKeyPassphrase = config.remoteIdentityPassphrase,
            .password = config.remotePassword,
            .connectTimeout = config.remoteConnectTimeout
        };

        remote::SshClient client { std::move(sshConfig) };
        client.connect();

        remote::SshCommandRunner runner { client };
        dispatch(runner);
    } else {
        LocalCommandRunner runner;
        dispatch(runner);
    }

    return 0;
}

auto printUsage() -> void
{
        std::cout
            << "Vaultium\n"
            << "Cross-platform database backup manager\n\n"
            << "Usage:\n"
            << "  vaultium backup --config /etc/vaultium/vaultium.conf\n"
            << "  vaultium loop --config /etc/vaultium/vaultium.conf\n"
            << "  vaultium agent --config /etc/vaultium/vaultium.conf\n"
            << "  vaultium verify --config <conf> --archive <file>\n"
            << "  vaultium restore --config <conf> --archive <file> --dest <dir> [--overwrite] [--dry-run]\n"
            << "  vaultium remote-test --config ./remote.conf\n"
            << "  vaultium remote-provision --config ./remote.conf\n"
            << "  vaultium remote --config ./remote.conf\n\n"
            << "Options:\n"
            << "  --config <path>   Config file path\n"
            << "  --archive <path>  Backup artifact (for verify/restore)\n"
            << "  --dest <path>     Restore destination directory\n"
            << "  --overwrite       Allow restore into a non-empty destination\n"
            << "  --dry-run         Validate restore without writing files\n"
            << "  --help            Show help\n";
}

[[nodiscard]] auto parseArguments(int argc, char* argv[]) -> CliOptions
{
    CliOptions options;

    if (argc <= 1) {
        options.showHelp = true;
        return options;
    }

    const std::string command { argv[1] };

    if (command == "backup") {
        options.runOnce = true;
    } else if (command == "loop") {
        options.runLoop = true;
    } else if (command == "agent") {
        options.runAgent = true;
    } else if (command == "remote") {
        options.runRemote = true;
    } else if (command == "remote-test") {
        options.runRemoteTest = true;
    } else if (command == "remote-provision") {
        options.runRemoteProvision = true;
    } else if (command == "restore") {
        options.runRestore = true;
    } else if (command == "verify") {
        options.runVerify = true;
    } else if (command == "inspect") {
        options.runInspect = true;
    } else if (command == "schedule") {
        options.runSchedule = true;
    } else if (command == "help" || command == "--help") {
        options.showHelp = true;
    } else {
        throw std::runtime_error("Unknown command: " + command);
    }

    // For `schedule`, the next positional token is the action.
    int firstFlag = 2;
    if (options.runSchedule && argc > 2 && argv[2][0] != '-') {
        options.scheduleAction = argv[2];
        firstFlag = 3;
    }

    const auto need = [&](int index) -> const char* {
        if (index + 1 >= argc) {
            throw std::runtime_error("Missing value for argument.");
        }
        return argv[index + 1];
    };

    for (int index = firstFlag; index < argc; ++index) {
        const std::string arg { argv[index] };

        if (arg == "--id") { options.schedId = need(index++); }
        else if (arg == "--name") { options.schedName = need(index++); }
        else if (arg == "--type") { options.schedType = need(index++); }
        else if (arg == "--time") { options.schedTime = need(index++); }
        else if (arg == "--dow") { options.schedDow = need(index++); }
        else if (arg == "--dom") { options.schedDom = need(index++); }
        else if (arg == "--once") { options.schedOnce = need(index++); }
        else if (arg == "--cron") { options.schedCron = need(index++); }
        else if (arg == "--backup-config") { options.schedConfig = need(index++); }
        else if (arg == "--backup-type") { options.schedBackupType = need(index++); }
        else if (arg == "--enabled") { options.schedEnabled = need(index++); }
        else if (arg == "--scope") { options.schedScope = need(index++); }
        else if (arg == "--help") {
            options.showHelp = true;
        } else if (arg == "--config") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--config requires a path.");
            }

            options.configPath = argv[++index];
        } else if (arg == "--archive") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--archive requires a path.");
            }

            options.archivePath = argv[++index];
        } else if (arg == "--dest") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--dest requires a path.");
            }

            options.restoreOptions.destination = argv[++index];
        } else if (arg == "--overwrite") {
            options.restoreOptions.overwrite = true;
        } else if (arg == "--dry-run") {
            options.restoreOptions.dryRun = true;
        } else if (arg == "--what") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--what requires a value (dir|disks|services|dbengines).");
            }
            options.inspectWhat = argv[++index];
        } else if (arg == "--path") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--path requires a value.");
            }
            options.inspectPath = argv[++index];
        } else if (arg == "--session") {
            options.inspectSession = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if ((options.runRestore || options.runVerify) && options.archivePath.empty()) {
        throw std::runtime_error("--archive is required for restore and verify.");
    }

    return options;
}

// -- Scheduler ---------------------------------------------------------------

[[nodiscard]] auto nowString() -> std::string
{
    return formatLocalTime(std::time(nullptr));
}

[[nodiscard]] auto genId() -> std::string
{
    static bool seeded = false;
    if (!seeded) { std::srand(static_cast<unsigned>(std::time(nullptr))); seeded = true; }
    return "sch_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(std::rand() % 9000 + 1000);
}

[[nodiscard]] auto parseHourMinute(const std::string& hhmm) -> std::pair<int, int>
{
    const auto colon = hhmm.find(':');
    if (colon == std::string::npos) {
        return { 2, 0 };
    }
    try {
        return { std::stoi(hhmm.substr(0, colon)), std::stoi(hhmm.substr(colon + 1)) };
    } catch (...) {
        return { 2, 0 };
    }
}

auto emitScheduleJson(std::ostream& out, const Schedule& s, std::time_t now) -> void
{
    out << "{\"id\":\"" << jsonEscape(s.id) << "\",\"name\":\"" << jsonEscape(s.name)
        << "\",\"backupType\":\"" << jsonEscape(s.backupType)
        << "\",\"type\":\"" << jsonEscape([&] {
               switch (s.type) {
               case ScheduleType::Once: return "once";
               case ScheduleType::Daily: return "daily";
               case ScheduleType::Weekly: return "weekly";
               case ScheduleType::Monthly: return "monthly";
               case ScheduleType::Cron: return "cron";
               } return "daily"; }())
        << "\",\"cron\":\"" << jsonEscape(s.cron)
        << "\",\"onceAt\":\"" << jsonEscape(s.onceAt)
        << "\",\"config\":\"" << jsonEscape(s.configPath)
        << "\",\"enabled\":" << (s.enabled ? "true" : "false")
        << ",\"summary\":\"" << jsonEscape(scheduleSummary(s))
        << "\",\"nextRun\":\"" << jsonEscape(computeNextRun(s, now))
        << "\",\"lastRun\":\"" << jsonEscape(s.lastRun.empty() ? "—" : s.lastRun)
        << "\",\"lastStatus\":\"" << jsonEscape(s.lastStatus.empty() ? "pending" : s.lastStatus)
        << "\",\"lastError\":\"" << jsonEscape(s.lastError)
        << "\",\"scope\":\"" << (s.scope == ScheduleScope::System ? "system" : "user")
        << "\",\"backend\":\"" << jsonEscape(schedulerBackendName(s.scope))
        << "\",\"installed\":" << (triggerInstalled(s.id, s.scope) ? "true" : "false") << "}";
}

// Executes a schedule's backup (used by the OS trigger). Updates the record.
[[nodiscard]] auto runScheduledBackup(Schedule& s, const std::string& exePath) -> int
{
    bool ok = false;
    std::string error;
    try {
        auto config = loadBackupConfig(s.configPath);
        if (config.executionMode == ExecutionMode::RemoteSsh) {
            ok = remote::runRemoteBackup(config) == 0;
            if (!ok) { error = "Remote backup failed (see log)."; }
        } else {
            BackupManager manager { std::move(config) };
            manager.runOnce();
            ok = true;
        }
    } catch (const std::exception& exception) {
        ok = false;
        error = exception.what();
    }

    const auto now = std::time(nullptr);
    s.lastRun = formatLocalTime(now);
    s.lastStatus = ok ? "completed" : "failed";
    s.lastError = ok ? std::string {} : error;

    // One-shot schedules disable themselves and remove their trigger after running.
    if (s.type == ScheduleType::Once) {
        s.enabled = false;
        uninstallTrigger(s.id);
    }
    s.nextRun = computeNextRun(s, now);
    s.updatedAt = formatLocalTime(now);
    saveSchedule(s);

    static_cast<void>(exePath);
    return ok ? 0 : 1;
}

[[nodiscard]] auto runSchedule(const CliOptions& options, const std::string& exePath) -> int
{
    const auto action = options.scheduleAction;
    const auto now = std::time(nullptr);

    if (action == "list") {
        std::cout << "{\"backend\":\"" << jsonEscape(schedulerBackendName()) << "\",\"schedules\":[";
        const auto schedules = loadSchedules();
        for (std::size_t i = 0; i < schedules.size(); ++i) {
            if (i) { std::cout << ","; }
            emitScheduleJson(std::cout, schedules[i], now);
        }
        std::cout << "]}\n";
        return 0;
    }

    if (action == "status") {
        std::cout << "{\"backend\":\"" << jsonEscape(schedulerBackendName())
                  << "\",\"supported\":" << (schedulerBackendName() != "unsupported" ? "true" : "false")
                  << "}\n";
        return 0;
    }

    if (action == "run") {
        auto loaded = loadSchedule(options.schedId);
        if (!loaded) { throw std::runtime_error("Schedule not found: " + options.schedId); }
        Logger::info("Running scheduled backup: " + loaded->name);
        return runScheduledBackup(*loaded, exePath);
    }

    if (action == "remove") {
        uninstallTrigger(options.schedId);
        removeSchedule(options.schedId);
        std::cout << "{\"ok\":true,\"id\":\"" << jsonEscape(options.schedId) << "\"}\n";
        return 0;
    }

    if (action == "set-enabled") {
        auto loaded = loadSchedule(options.schedId);
        if (!loaded) { throw std::runtime_error("Schedule not found: " + options.schedId); }
        loaded->enabled = (options.schedEnabled == "true");
        loaded->nextRun = computeNextRun(*loaded, now);
        loaded->updatedAt = nowString();
        saveSchedule(*loaded);
        const auto trig = loaded->enabled ? installTrigger(*loaded, exePath) : uninstallTrigger(loaded->id);
        std::cout << "{\"ok\":" << (trig.ok ? "true" : "false") << ",\"id\":\"" << jsonEscape(loaded->id)
                  << "\",\"message\":\"" << jsonEscape(trig.message) << "\"}\n";
        return trig.ok ? 0 : 1;
    }

    if (action == "repair") {
        auto loaded = loadSchedule(options.schedId);
        if (!loaded) { throw std::runtime_error("Schedule not found: " + options.schedId); }
        const auto trig = loaded->enabled ? installTrigger(*loaded, exePath) : uninstallTrigger(loaded->id);
        std::cout << "{\"ok\":" << (trig.ok ? "true" : "false") << ",\"id\":\"" << jsonEscape(loaded->id)
                  << "\",\"message\":\"" << jsonEscape(trig.message) << "\"}\n";
        return trig.ok ? 0 : 1;
    }

    if (action == "save") {
        // Build (or update) the schedule from flags, with validation.
        Schedule s;
        if (!options.schedId.empty()) {
            if (auto existing = loadSchedule(options.schedId)) { s = *existing; }
            s.id = options.schedId;
        } else {
            s.id = genId();
            s.createdAt = nowString();
        }
        s.name = options.schedName;
        s.backupType = options.schedBackupType;
        s.configPath = options.schedConfig;
        s.enabled = (options.schedEnabled == "true");
        s.scope = (options.schedScope == "system") ? ScheduleScope::System : ScheduleScope::User;
        s.updatedAt = nowString();

        const auto [hour, minute] = parseHourMinute(options.schedTime);
        if (options.schedType == "once") {
            s.type = ScheduleType::Once;
            s.onceAt = options.schedOnce;
            s.cron.clear();
        } else if (options.schedType == "weekly") {
            s.type = ScheduleType::Weekly;
            s.cron = cronForWeekly(std::stoi(options.schedDow), hour, minute);
        } else if (options.schedType == "monthly") {
            s.type = ScheduleType::Monthly;
            s.cron = cronForMonthly(std::stoi(options.schedDom), hour, minute);
        } else if (options.schedType == "cron") {
            s.type = ScheduleType::Cron;
            s.cron = options.schedCron;
        } else {
            s.type = ScheduleType::Daily;
            s.cron = cronForDaily(hour, minute);
        }

        // Validation.
        std::string validationError;
        if (s.name.empty()) { validationError = "Name is required."; }
        else if (s.configPath.empty() || !std::filesystem::exists(s.configPath)) { validationError = "Backup configuration is missing."; }
        else if (s.type == ScheduleType::Once && computeNextRun(s, now) == "—") { validationError = "The one-time date must be in the future (YYYY-MM-DD HH:MM)."; }
        else if (s.type != ScheduleType::Once && !isValidCron(s.cron)) { validationError = "Invalid schedule expression: " + s.cron; }

        if (!validationError.empty()) {
            std::cout << "{\"ok\":false,\"message\":\"" << jsonEscape(validationError) << "\"}\n";
            return 1;
        }

        s.nextRun = computeNextRun(s, now);
        saveSchedule(s);

        TriggerResult trig { true, schedulerBackendName(), "Saved (disabled)." };
        if (s.enabled) {
            trig = installTrigger(s, exePath);
        } else {
            uninstallTrigger(s.id);
        }

        std::cout << "{\"ok\":" << (trig.ok ? "true" : "false") << ",\"id\":\"" << jsonEscape(s.id)
                  << "\",\"backend\":\"" << jsonEscape(trig.backend)
                  << "\",\"message\":\"" << jsonEscape(trig.message) << "\"}\n";
        return trig.ok ? 0 : 1;
    }

    throw std::runtime_error("Unknown schedule action: " + action);
}

} // namespace

auto run(int argc, char* argv[]) -> int
{
    try {
        const auto options = parseArguments(argc, argv);

        if (options.showHelp) {
            printUsage();
            return 0;
        }

        // The scheduler manages its own config references, not the main --config.
        if (options.runSchedule) {
            const std::string exePath = std::filesystem::weakly_canonical(argv[0]).string();
            return runSchedule(options, exePath);
        }

        Logger::info("Loading config: " + options.configPath.string());

        // Discovery only needs connection details, not a complete backup config.
        auto config = loadBackupConfig(options.configPath, /*validate=*/ !options.runInspect);

        if (options.runRemoteTest) {
            return remote::runRemoteTest(config);
        }

        if (options.runRemoteProvision) {
            return remote::runRemoteProvision(config);
        }

        if (options.runRemote) {
            return remote::runRemoteBackup(config);
        }

        if (options.runInspect) {
            return runInspect(config, options);
        }

        if (options.runAgent) {
            return agent::runAgent(config);
        }

        BackupManager manager { std::move(config) };

        if (options.runVerify) {
            return manager.verify(options.archivePath) ? 0 : 1;
        }

        if (options.runRestore) {
            manager.restore(options.archivePath, options.restoreOptions);
            return 0;
        }

        if (options.runOnce) {
            manager.runOnce();
            return 0;
        }

        if (options.runLoop) {
            manager.runLoop();
            return 0;
        }

        printUsage();
        return 1;
    } catch (const std::exception& exception) {
        Logger::error(exception.what());
        return 1;
    }
}

} // namespace vaultium::cli