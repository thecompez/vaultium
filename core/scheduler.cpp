module;

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

module vaultium_core_scheduler;

import vaultium_core_inventory; // LocalCommandRunner

namespace vaultium {
namespace {

[[nodiscard]] auto trim(std::string value) -> std::string
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] auto split(const std::string& value, char delimiter) -> std::vector<std::string>
{
    std::vector<std::string> out;
    std::stringstream stream { value };
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        out.push_back(item);
    }
    return out;
}

// -- Cron -------------------------------------------------------------------

struct CronField {
    bool wildcard {};
    std::vector<int> values;
    [[nodiscard]] auto matches(int v) const -> bool
    {
        return wildcard || std::ranges::find(values, v) != values.end();
    }
};

struct CronSpec {
    bool valid {};
    CronField minute, hour, dom, month, dow;
};

[[nodiscard]] auto parseField(const std::string& field, int lo, int hi, bool isDow) -> std::optional<CronField>
{
    CronField result;
    result.wildcard = (field == "*");

    try {
        for (const auto& partRaw : split(field, ',')) {
            const auto part = trim(partRaw);
            if (part.empty()) {
                return std::nullopt;
            }

            int step = 1;
            std::string range = part;
            if (const auto slash = part.find('/'); slash != std::string::npos) {
                range = part.substr(0, slash);
                step = std::stoi(part.substr(slash + 1));
                if (step <= 0) {
                    return std::nullopt;
                }
            }

            int a = lo;
            int b = hi;
            if (range != "*") {
                if (const auto dash = range.find('-'); dash != std::string::npos) {
                    a = std::stoi(range.substr(0, dash));
                    b = std::stoi(range.substr(dash + 1));
                } else {
                    a = b = std::stoi(range);
                }
            }

            for (int v = a; v <= b; v += step) {
                int n = v;
                if (isDow && n == 7) {
                    n = 0; // Sunday
                }
                if (n < lo || n > hi) {
                    return std::nullopt;
                }
                result.values.push_back(n);
            }
        }
    } catch (...) {
        return std::nullopt;
    }

    std::ranges::sort(result.values);
    result.values.erase(std::ranges::unique(result.values).begin(), result.values.end());
    if (result.values.empty()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] auto parseCron(const std::string& expr) -> CronSpec
{
    CronSpec spec;
    const auto tokens = [&] {
        std::vector<std::string> out;
        std::stringstream stream { expr };
        std::string token;
        while (stream >> token) {
            out.push_back(token);
        }
        return out;
    }();

    if (tokens.size() != 5) {
        return spec;
    }

    const auto minute = parseField(tokens[0], 0, 59, false);
    const auto hour = parseField(tokens[1], 0, 23, false);
    const auto dom = parseField(tokens[2], 1, 31, false);
    const auto month = parseField(tokens[3], 1, 12, false);
    const auto dow = parseField(tokens[4], 0, 6, true);

    if (!minute || !hour || !dom || !month || !dow) {
        return spec;
    }

    spec.valid = true;
    spec.minute = *minute;
    spec.hour = *hour;
    spec.dom = *dom;
    spec.month = *month;
    spec.dow = *dow;
    return spec;
}

[[nodiscard]] auto matchesCron(const CronSpec& spec, const std::tm& tm) -> bool
{
    if (!spec.minute.matches(tm.tm_min) || !spec.hour.matches(tm.tm_hour)
        || !spec.month.matches(tm.tm_mon + 1)) {
        return false;
    }

    const bool domRestricted = !spec.dom.wildcard;
    const bool dowRestricted = !spec.dow.wildcard;
    const bool domHit = spec.dom.matches(tm.tm_mday);
    const bool dowHit = spec.dow.matches(tm.tm_wday);

    if (domRestricted && dowRestricted) {
        return domHit || dowHit; // cron's day-of-month / day-of-week OR quirk
    }
    return domHit && dowHit;
}

[[nodiscard]] auto twoDigit(int v) -> std::string
{
    return (v < 10 ? "0" : "") + std::to_string(v);
}

} // namespace

auto isValidCron(const std::string& expr) -> bool
{
    return parseCron(expr).valid;
}

auto nextCronTime(const std::string& expr, std::time_t from) -> std::time_t
{
    const auto spec = parseCron(expr);
    if (!spec.valid) {
        return 0;
    }

    std::time_t candidate = (from / 60 + 1) * 60; // next whole minute strictly after `from`
    const long horizon = 366L * 24 * 60;          // one-year search window

    for (long i = 0; i < horizon; ++i, candidate += 60) {
        std::tm tm {};
        localtime_r(&candidate, &tm);
        if (matchesCron(spec, tm)) {
            return candidate;
        }
    }
    return 0;
}

auto cronForDaily(int hour, int minute) -> std::string
{
    return std::to_string(minute) + " " + std::to_string(hour) + " * * *";
}

auto cronForWeekly(int dayOfWeek, int hour, int minute) -> std::string
{
    return std::to_string(minute) + " " + std::to_string(hour) + " * * " + std::to_string(dayOfWeek);
}

auto cronForMonthly(int dayOfMonth, int hour, int minute) -> std::string
{
    return std::to_string(minute) + " " + std::to_string(hour) + " " + std::to_string(dayOfMonth) + " * *";
}

auto formatLocalTime(std::time_t when) -> std::string
{
    if (when == 0) {
        return "—";
    }
    std::tm tm {};
    localtime_r(&when, &tm);
    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return stream.str();
}

[[nodiscard]] auto parseOnceAt(const std::string& onceAt) -> std::time_t
{
    std::tm tm {};
    std::istringstream stream { onceAt };
    stream >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if (stream.fail()) {
        return 0;
    }
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

auto computeNextRun(const Schedule& schedule, std::time_t from) -> std::string
{
    if (!schedule.enabled) {
        return "—";
    }
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        return (at > from) ? formatLocalTime(at) : "—";
    }
    const auto next = nextCronTime(schedule.cron, from);
    return next == 0 ? "—" : formatLocalTime(next);
}

auto scheduleSummary(const Schedule& schedule) -> std::string
{
    static const std::array<const char*, 7> days {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };

    const auto tokens = [&] {
        std::vector<std::string> out;
        std::stringstream stream { schedule.cron };
        std::string token;
        while (stream >> token) {
            out.push_back(token);
        }
        return out;
    }();

    const auto hhmm = [&]() -> std::string {
        if (tokens.size() == 5) {
            try {
                return twoDigit(std::stoi(tokens[1])) + ":" + twoDigit(std::stoi(tokens[0]));
            } catch (...) {
            }
        }
        return {};
    }();

    switch (schedule.type) {
    case ScheduleType::Once:
        return "Once on " + schedule.onceAt;
    case ScheduleType::Daily:
        return "Every day at " + hhmm;
    case ScheduleType::Weekly: {
        int dow = 0;
        try { dow = std::stoi(tokens.at(4)); } catch (...) {}
        return std::string("Every ") + days.at(static_cast<std::size_t>(dow % 7)) + " at " + hhmm;
    }
    case ScheduleType::Monthly:
        return "Day " + (tokens.size() == 5 ? tokens[2] : std::string("?")) + " of each month at " + hhmm;
    case ScheduleType::Cron:
        return "Cron: " + schedule.cron;
    }
    return schedule.cron;
}

// -- Persistence ------------------------------------------------------------

namespace {

[[nodiscard]] auto typeToString(ScheduleType t) -> std::string
{
    switch (t) {
    case ScheduleType::Once:    return "once";
    case ScheduleType::Daily:   return "daily";
    case ScheduleType::Weekly:  return "weekly";
    case ScheduleType::Monthly: return "monthly";
    case ScheduleType::Cron:    return "cron";
    }
    return "daily";
}

[[nodiscard]] auto typeFromString(const std::string& s) -> ScheduleType
{
    if (s == "once") return ScheduleType::Once;
    if (s == "weekly") return ScheduleType::Weekly;
    if (s == "monthly") return ScheduleType::Monthly;
    if (s == "cron") return ScheduleType::Cron;
    return ScheduleType::Daily;
}

[[nodiscard]] auto scopeToString(ScheduleScope s) -> std::string
{
    return s == ScheduleScope::System ? "system" : "user";
}

[[nodiscard]] auto scopeFromString(const std::string& s) -> ScheduleScope
{
    return s == "system" ? ScheduleScope::System : ScheduleScope::User;
}

[[nodiscard]] auto sanitize(std::string value) -> std::string
{
    std::ranges::replace(value, '\n', ' ');
    std::ranges::replace(value, '\r', ' ');
    return value;
}

[[nodiscard]] auto homeDir() -> std::filesystem::path
{
    const char* home = std::getenv("HOME");
    return home != nullptr ? std::filesystem::path { home } : std::filesystem::path { "/tmp" };
}

} // namespace

auto scheduleDirectory() -> std::filesystem::path
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    auto dir = homeDir() / "Library" / "Application Support" / "Vaultium" / "schedules";
#else
    auto dir = homeDir() / ".config" / "vaultium" / "schedules";
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

auto serializeSchedule(const Schedule& s) -> std::string
{
    std::ostringstream out;
    out << "ID=" << sanitize(s.id) << '\n'
        << "NAME=" << sanitize(s.name) << '\n'
        << "BACKUP_TYPE=" << sanitize(s.backupType) << '\n'
        << "CONFIG=" << sanitize(s.configPath) << '\n'
        << "TYPE=" << typeToString(s.type) << '\n'
        << "SCOPE=" << scopeToString(s.scope) << '\n'
        << "CRON=" << sanitize(s.cron) << '\n'
        << "ONCE_AT=" << sanitize(s.onceAt) << '\n'
        << "ENABLED=" << (s.enabled ? "true" : "false") << '\n'
        << "LAST_RUN=" << sanitize(s.lastRun) << '\n'
        << "NEXT_RUN=" << sanitize(s.nextRun) << '\n'
        << "LAST_STATUS=" << sanitize(s.lastStatus) << '\n'
        << "LAST_ERROR=" << sanitize(s.lastError) << '\n'
        << "CREATED=" << sanitize(s.createdAt) << '\n'
        << "UPDATED=" << sanitize(s.updatedAt) << '\n';
    return out.str();
}

auto parseSchedule(const std::string& text) -> Schedule
{
    Schedule s;
    std::stringstream stream { text };
    std::string line;
    while (std::getline(stream, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        if (key == "ID") s.id = value;
        else if (key == "NAME") s.name = value;
        else if (key == "BACKUP_TYPE") s.backupType = value;
        else if (key == "CONFIG") s.configPath = value;
        else if (key == "TYPE") s.type = typeFromString(value);
        else if (key == "SCOPE") s.scope = scopeFromString(value);
        else if (key == "CRON") s.cron = value;
        else if (key == "ONCE_AT") s.onceAt = value;
        else if (key == "ENABLED") s.enabled = (value == "true");
        else if (key == "LAST_RUN") s.lastRun = value;
        else if (key == "NEXT_RUN") s.nextRun = value;
        else if (key == "LAST_STATUS") s.lastStatus = value;
        else if (key == "LAST_ERROR") s.lastError = value;
        else if (key == "CREATED") s.createdAt = value;
        else if (key == "UPDATED") s.updatedAt = value;
    }
    return s;
}

auto loadSchedules() -> std::vector<Schedule>
{
    std::vector<Schedule> result;
    const auto dir = scheduleDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".schedule") {
            std::ifstream file { entry.path() };
            std::stringstream buffer;
            buffer << file.rdbuf();
            auto s = parseSchedule(buffer.str());
            if (!s.id.empty()) {
                result.push_back(std::move(s));
            }
        }
    }
    std::ranges::sort(result, [](const Schedule& a, const Schedule& b) { return a.name < b.name; });
    return result;
}

auto loadSchedule(const std::string& id) -> std::optional<Schedule>
{
    const auto path = scheduleDirectory() / (id + ".schedule");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::nullopt;
    }
    std::ifstream file { path };
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseSchedule(buffer.str());
}

auto saveSchedule(const Schedule& schedule) -> void
{
    const auto path = scheduleDirectory() / (schedule.id + ".schedule");
    std::ofstream file { path, std::ios::trunc };
    file << serializeSchedule(schedule);
}

auto removeSchedule(const std::string& id) -> void
{
    std::error_code ec;
    std::filesystem::remove(scheduleDirectory() / (id + ".schedule"), ec);
}

// -- OS trigger generation --------------------------------------------------

namespace {

// Calendar "dicts" (field -> value) covering the schedule's fire times.
// Wildcard fields are omitted (launchd/systemd treat absence as "every").
[[nodiscard]] auto calendarMatrix(const Schedule& schedule)
    -> std::vector<std::vector<std::pair<std::string, int>>>
{
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        std::tm tm {};
        localtime_r(&at, &tm);
        return { { { "Month", tm.tm_mon + 1 }, { "Day", tm.tm_mday },
                   { "Hour", tm.tm_hour }, { "Minute", tm.tm_min } } };
    }

    const auto spec = parseCron(schedule.cron);
    if (!spec.valid) {
        return {};
    }

    struct Dim { std::string key; const CronField* field; };
    const std::array<Dim, 5> dims {
        Dim { "Minute", &spec.minute }, Dim { "Hour", &spec.hour },
        Dim { "Day", &spec.dom }, Dim { "Month", &spec.month },
        Dim { "Weekday", &spec.dow }
    };

    std::vector<std::vector<std::pair<std::string, int>>> rows { {} };
    for (const auto& dim : dims) {
        if (dim.field->wildcard) {
            continue; // omit wildcard dimensions
        }
        std::vector<std::vector<std::pair<std::string, int>>> expanded;
        for (const auto& row : rows) {
            for (const int v : dim.field->values) {
                auto next = row;
                next.emplace_back(dim.key, v);
                expanded.push_back(std::move(next));
                if (expanded.size() > 366) {
                    return expanded; // safety cap
                }
            }
        }
        rows = std::move(expanded);
    }
    return rows;
}

} // namespace

auto generateLaunchdPlist(const Schedule& schedule, const std::string& exePath) -> std::string
{
    const auto rows = calendarMatrix(schedule);

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n<dict>\n"
        << "  <key>Label</key><string>com.vaultium." << schedule.id << "</string>\n"
        << "  <key>ProgramArguments</key>\n  <array>\n"
        << "    <string>" << exePath << "</string>\n"
        << "    <string>schedule</string>\n    <string>run</string>\n"
        << "    <string>--id</string>\n    <string>" << schedule.id << "</string>\n"
        << "  </array>\n";

    const auto emitDict = [&out](const std::vector<std::pair<std::string, int>>& dict) {
        out << "    <dict>\n";
        for (const auto& [k, v] : dict) {
            out << "      <key>" << k << "</key><integer>" << v << "</integer>\n";
        }
        out << "    </dict>\n";
    };

    out << "  <key>StartCalendarInterval</key>\n";
    if (rows.size() == 1) {
        emitDict(rows.front());
    } else {
        out << "  <array>\n";
        for (const auto& row : rows) {
            emitDict(row);
        }
        out << "  </array>\n";
    }

    out << "  <key>StandardErrorPath</key><string>" << (scheduleDirectory() / (schedule.id + ".log")).string()
        << "</string>\n"
        << "  <key>StandardOutPath</key><string>" << (scheduleDirectory() / (schedule.id + ".log")).string()
        << "</string>\n"
        << "</dict>\n</plist>\n";
    return out.str();
}

auto generateSystemdService(const Schedule& schedule, const std::string& exePath) -> std::string
{
    std::ostringstream out;
    out << "[Unit]\nDescription=Vaultium scheduled backup: " << schedule.name << "\n\n"
        << "[Service]\nType=oneshot\n"
        << "ExecStart=" << exePath << " schedule run --id " << schedule.id << "\n";
    return out.str();
}

auto generateSystemdTimer(const Schedule& schedule) -> std::string
{
    std::string onCalendar;
    const auto tokens = split(schedule.cron, ' ');
    const auto field = [&](std::size_t i) -> std::string {
        return i < tokens.size() ? trim(tokens[i]) : "*";
    };

    if (schedule.type == ScheduleType::Once) {
        onCalendar = schedule.onceAt + ":00";
    } else {
        const auto minute = field(0);
        const auto hour = field(1);
        const auto dom = field(2);
        const auto month = field(3);
        const auto dow = field(4);
        const auto star = [](const std::string& v) { return v == "*" ? std::string("*") : v; };
        std::string prefix;
        if (dow != "*") {
            static const std::array<const char*, 7> names { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            try { prefix = std::string(names.at(static_cast<std::size_t>(std::stoi(dow) % 7))) + " "; } catch (...) {}
        }
        onCalendar = prefix + "*-" + star(month) + "-" + star(dom) + " "
            + (hour == "*" ? "*" : (hour.size() < 2 ? "0" + hour : hour)) + ":"
            + (minute == "*" ? "*" : (minute.size() < 2 ? "0" + minute : minute)) + ":00";
    }

    std::ostringstream out;
    out << "[Unit]\nDescription=Vaultium schedule timer: " << schedule.name << "\n\n"
        << "[Timer]\nOnCalendar=" << onCalendar << "\nPersistent=true\n\n"
        << "[Install]\nWantedBy=timers.target\n";
    return out.str();
}

auto generateCrontabLine(const Schedule& schedule, const std::string& exePath) -> std::string
{
    std::string cron = schedule.cron;
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        std::tm tm {};
        localtime_r(&at, &tm);
        cron = std::to_string(tm.tm_min) + " " + std::to_string(tm.tm_hour) + " "
            + std::to_string(tm.tm_mday) + " " + std::to_string(tm.tm_mon + 1) + " *";
    }
    return cron + " " + exePath + " schedule run --id " + schedule.id
        + " # vaultium:" + schedule.id;
}

// -- OS install / uninstall -------------------------------------------------

namespace {

[[nodiscard]] auto systemdAvailable(ICommandRunner& runner) -> bool
{
    return runner.run("command -v systemctl >/dev/null 2>&1 && "
                      "systemctl --user show-environment >/dev/null 2>&1 && echo ok").output.find("ok")
        != std::string::npos;
}

// Runs a shell command with elevated privileges, prompting the user via the OS.
// (macOS: AppleScript admin prompt; Linux: pkexec/sudo.) Commands must avoid
// single quotes; our generated commands only reference quote-free paths.
[[maybe_unused]] [[nodiscard]] auto runElevated(const std::string& shellCommand) -> CommandResult
{
    LocalCommandRunner runner;
#if defined(VAULTIUM_PLATFORM_MACOS)
    std::string escaped; // escape for an AppleScript string literal
    for (const char c : shellCommand) {
        if (c == '\\' || c == '"') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return runner.run("osascript -e 'do shell script \"" + escaped + "\" with administrator privileges' 2>&1");
#elif defined(VAULTIUM_PLATFORM_LINUX)
    std::string quoted; // for sh -c '<cmd>'
    for (const char c : shellCommand) { if (c == '\'') quoted += "'\\''"; else quoted.push_back(c); }
    if (runner.run("command -v pkexec >/dev/null 2>&1 && echo y").output.find('y') != std::string::npos) {
        return runner.run("pkexec sh -c '" + quoted + "' 2>&1");
    }
    return runner.run("sudo -n sh -c '" + quoted + "' 2>&1");
#else
    static_cast<void>(shellCommand);
    return { 1, {} };
#endif
}

} // namespace

auto launchdPlistPath(const std::string& id, ScheduleScope scope) -> std::filesystem::path
{
    const std::string file = "com.vaultium." + id + ".plist";
    if (scope == ScheduleScope::System) {
        return std::filesystem::path { "/Library/LaunchDaemons" } / file;
    }
    return homeDir() / "Library" / "LaunchAgents" / file;
}

auto schedulerBackendName(ScheduleScope scope) -> std::string
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    return scope == ScheduleScope::System ? "launchd (system)" : "launchd";
#elif defined(VAULTIUM_PLATFORM_LINUX)
    LocalCommandRunner runner;
    if (scope == ScheduleScope::System) {
        return "systemd (system)";
    }
    return systemdAvailable(runner) ? "systemd" : "cron";
#else
    static_cast<void>(scope);
    return "unsupported";
#endif
}

auto schedulerBackendName() -> std::string
{
    return schedulerBackendName(ScheduleScope::User);
}

auto triggerInstalled(const std::string& id, ScheduleScope scope) -> bool
{
    std::error_code ec;
#if defined(VAULTIUM_PLATFORM_MACOS)
    return std::filesystem::exists(launchdPlistPath(id, scope), ec);
#elif defined(VAULTIUM_PLATFORM_LINUX)
    if (scope == ScheduleScope::System) {
        return std::filesystem::exists(std::filesystem::path { "/etc/systemd/system" } / ("vaultium-" + id + ".timer"), ec);
    }
    if (std::filesystem::exists(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".timer"), ec)) {
        return true;
    }
    LocalCommandRunner runner;
    return runner.run("crontab -l 2>/dev/null | grep -F 'vaultium:" + id + "' >/dev/null && echo yes").output.find("yes")
        != std::string::npos;
#else
    static_cast<void>(id); static_cast<void>(scope);
    return false;
#endif
}

auto triggerInstalled(const std::string& id) -> bool
{
    return triggerInstalled(id, ScheduleScope::User) || triggerInstalled(id, ScheduleScope::System);
}

auto installTrigger([[maybe_unused]] const Schedule& schedule, [[maybe_unused]] const std::string& exePath) -> TriggerResult
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    if (schedule.scope == ScheduleScope::System) {
        const auto temp = std::filesystem::path { "/tmp" } / ("com.vaultium." + schedule.id + ".plist");
        { std::ofstream file { temp, std::ios::trunc }; file << generateLaunchdPlist(schedule, exePath); }
        const auto dest = launchdPlistPath(schedule.id, ScheduleScope::System).string();
        const auto cmd =
            "mkdir -p /Library/LaunchDaemons && cp " + temp.string() + " " + dest +
            " && chown root:wheel " + dest + " && chmod 644 " + dest +
            " && launchctl unload " + dest + " 2>/dev/null; launchctl load " + dest;
        const auto res = runElevated(cmd);
        std::error_code ec; std::filesystem::remove(temp, ec);
        return { res.exitCode == 0, "launchd (system)",
                 res.exitCode == 0 ? "System LaunchDaemon installed." : ("Admin install failed or cancelled: " + res.output) };
    }
    const auto dir = homeDir() / "Library" / "LaunchAgents";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto plist = launchdPlistPath(schedule.id, ScheduleScope::User);
    { std::ofstream file { plist, std::ios::trunc }; file << generateLaunchdPlist(schedule, exePath); }
    LocalCommandRunner runner;
    runner.run("launchctl unload '" + plist.string() + "' 2>/dev/null");
    const auto load = runner.run("launchctl load '" + plist.string() + "' 2>&1");
    return { load.exitCode == 0, "launchd",
             load.exitCode == 0 ? "LaunchAgent installed." : ("launchctl load failed: " + load.output) };
#elif defined(VAULTIUM_PLATFORM_LINUX)
    LocalCommandRunner runner;
    if (schedule.scope == ScheduleScope::System) {
        const auto svc = std::filesystem::path { "/tmp" } / ("vaultium-" + schedule.id + ".service");
        const auto tmr = std::filesystem::path { "/tmp" } / ("vaultium-" + schedule.id + ".timer");
        { std::ofstream { svc, std::ios::trunc } << generateSystemdService(schedule, exePath); }
        { std::ofstream { tmr, std::ios::trunc } << generateSystemdTimer(schedule); }
        const auto cmd =
            "cp " + svc.string() + " /etc/systemd/system/vaultium-" + schedule.id + ".service && "
            "cp " + tmr.string() + " /etc/systemd/system/vaultium-" + schedule.id + ".timer && "
            "systemctl daemon-reload && systemctl enable --now vaultium-" + schedule.id + ".timer";
        const auto res = runElevated(cmd);
        std::error_code ec; std::filesystem::remove(svc, ec); std::filesystem::remove(tmr, ec);
        return { res.exitCode == 0, "systemd (system)",
                 res.exitCode == 0 ? "System timer installed." : ("Admin install failed or cancelled: " + res.output) };
    }
    if (systemdAvailable(runner)) {
        const auto dir = homeDir() / ".config" / "systemd" / "user";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        { std::ofstream { dir / ("vaultium-" + schedule.id + ".service"), std::ios::trunc } << generateSystemdService(schedule, exePath); }
        { std::ofstream { dir / ("vaultium-" + schedule.id + ".timer"), std::ios::trunc } << generateSystemdTimer(schedule); }
        runner.run("systemctl --user daemon-reload");
        const auto en = runner.run("systemctl --user enable --now vaultium-" + schedule.id + ".timer 2>&1");
        return { en.exitCode == 0, "systemd",
                 en.exitCode == 0 ? "systemd --user timer installed." : ("enable failed: " + en.output) };
    }
    const auto line = generateCrontabLine(schedule, exePath);
    const auto cmd = "( crontab -l 2>/dev/null | grep -vF 'vaultium:" + schedule.id + "'; echo " +
        std::string("'") + line + "' ) | crontab - 2>&1";
    const auto res = runner.run(cmd);
    return { res.exitCode == 0, "cron", res.exitCode == 0 ? "cron entry installed." : ("crontab failed: " + res.output) };
#else
    return { false, "unsupported", "Scheduling is not implemented on this platform yet." };
#endif
}

auto uninstallTrigger([[maybe_unused]] const std::string& id) -> TriggerResult
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    LocalCommandRunner runner;
    const auto userPlist = launchdPlistPath(id, ScheduleScope::User);
    runner.run("launchctl unload '" + userPlist.string() + "' 2>/dev/null");
    std::error_code ec;
    std::filesystem::remove(userPlist, ec);

    const auto sysPlist = launchdPlistPath(id, ScheduleScope::System);
    if (std::filesystem::exists(sysPlist, ec)) {
        runElevated("launchctl unload " + sysPlist.string() + " 2>/dev/null; rm -f " + sysPlist.string());
    }
    return { true, "launchd", "Trigger removed." };
#elif defined(VAULTIUM_PLATFORM_LINUX)
    LocalCommandRunner runner;
    runner.run("systemctl --user disable --now vaultium-" + id + ".timer 2>/dev/null");
    std::error_code ec;
    std::filesystem::remove(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".timer"), ec);
    std::filesystem::remove(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".service"), ec);
    runner.run("systemctl --user daemon-reload 2>/dev/null");
    runner.run("( crontab -l 2>/dev/null | grep -vF 'vaultium:" + id + "' ) | crontab - 2>/dev/null");
    if (std::filesystem::exists(std::filesystem::path { "/etc/systemd/system" } / ("vaultium-" + id + ".timer"), ec)) {
        runElevated("systemctl disable --now vaultium-" + id + ".timer 2>/dev/null; "
                    "rm -f /etc/systemd/system/vaultium-" + id + ".timer /etc/systemd/system/vaultium-" + id + ".service; "
                    "systemctl daemon-reload");
    }
    return { true, "systemd", "Trigger removed." };
#else
    return { false, "unsupported", "Not implemented." };
#endif
}

} // namespace vaultium
