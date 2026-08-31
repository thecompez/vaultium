module;

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module vaultium_core_scheduler;

import vaultium_core_inventory;

namespace vaultium {
namespace {

[[nodiscard]] auto trim(std::string value) -> std::string
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] auto split(const std::string& value, char delimiter) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::stringstream stream { value };
    std::string item;
    while (std::getline(stream, item, delimiter)) result.push_back(item);
    return result;
}

[[nodiscard]] auto shellQuote(const std::string& value) -> std::string
{
    std::string result { "'" };
    for (const char character : value) {
        if (character == '\'') result += "'\\''";
        else result.push_back(character);
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] auto xmlEscape(const std::string& value) -> std::string
{
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

[[nodiscard]] auto systemdQuote(const std::string& value) -> std::string
{
    std::string result { "\"" };
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] auto oneLine(std::string value) -> std::string
{
    std::ranges::replace(value, '\n', ' ');
    std::ranges::replace(value, '\r', ' ');
    return value;
}

struct CronField {
    bool unrestricted {};
    std::vector<int> values;

    [[nodiscard]] auto matches(int value) const -> bool
    {
        return unrestricted || std::ranges::find(values, value) != values.end();
    }
};

struct CronSpec {
    bool valid {};
    CronField minute;
    CronField hour;
    CronField dom;
    CronField month;
    CronField dow;
};

[[nodiscard]] auto parseField(const std::string& field, int low, int high, bool dayOfWeek)
    -> std::optional<CronField>
{
    CronField result;
    result.unrestricted = field == "*";
    const int inputHigh = dayOfWeek ? 7 : high;

    try {
        for (const auto& rawPart : split(field, ',')) {
            const auto part = trim(rawPart);
            if (part.empty()) return std::nullopt;

            int step = 1;
            std::string range = part;
            if (const auto slash = part.find('/'); slash != std::string::npos) {
                if (part.find('/', slash + 1) != std::string::npos) return std::nullopt;
                range = part.substr(0, slash);
                const auto stepText = part.substr(slash + 1);
                if (stepText.empty()) return std::nullopt;
                step = std::stoi(stepText);
                if (step <= 0) return std::nullopt;
            }

            int first = low;
            int last = inputHigh;
            if (range != "*") {
                if (const auto dash = range.find('-'); dash != std::string::npos) {
                    if (range.find('-', dash + 1) != std::string::npos) return std::nullopt;
                    first = std::stoi(range.substr(0, dash));
                    last = std::stoi(range.substr(dash + 1));
                } else {
                    first = last = std::stoi(range);
                }
            }

            if (first < low || last > inputHigh || first > last) return std::nullopt;

            for (int value = first; value <= last; value += step) {
                const int normalized = dayOfWeek && value == 7 ? 0 : value;
                if (normalized < low || normalized > high) return std::nullopt;
                result.values.push_back(normalized);
            }
        }
    } catch (...) {
        return std::nullopt;
    }

    std::ranges::sort(result.values);
    const auto unique = std::ranges::unique(result.values);
    result.values.erase(unique.begin(), unique.end());
    if (result.values.empty()) return std::nullopt;
    return result;
}

[[nodiscard]] auto parseCron(const std::string& expression) -> CronSpec
{
    std::vector<std::string> tokens;
    std::stringstream stream { expression };
    std::string token;
    while (stream >> token) tokens.push_back(token);
    if (tokens.size() != 5) return {};

    const auto minute = parseField(tokens[0], 0, 59, false);
    const auto hour = parseField(tokens[1], 0, 23, false);
    const auto dom = parseField(tokens[2], 1, 31, false);
    const auto month = parseField(tokens[3], 1, 12, false);
    const auto dow = parseField(tokens[4], 0, 6, true);
    if (!minute || !hour || !dom || !month || !dow) return {};

    return CronSpec { true, *minute, *hour, *dom, *month, *dow };
}

[[nodiscard]] auto matchesCron(const CronSpec& spec, const std::tm& tm) -> bool
{
    if (!spec.minute.matches(tm.tm_min) || !spec.hour.matches(tm.tm_hour)
        || !spec.month.matches(tm.tm_mon + 1)) return false;

    const bool domRestricted = !spec.dom.unrestricted;
    const bool dowRestricted = !spec.dow.unrestricted;
    const bool domMatch = spec.dom.matches(tm.tm_mday);
    const bool dowMatch = spec.dow.matches(tm.tm_wday);

    if (domRestricted && dowRestricted) return domMatch || dowMatch;
    return domMatch && dowMatch;
}

[[nodiscard]] auto twoDigit(int value) -> std::string
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

[[nodiscard]] auto parseOnceAt(const std::string& value) -> std::time_t
{
    std::tm time {};
    std::istringstream stream { value };
    stream >> std::get_time(&time, "%Y-%m-%d %H:%M");
    if (stream.fail()) return 0;
    time.tm_isdst = -1;
    const auto converted = std::mktime(&time);
    if (converted == static_cast<std::time_t>(-1)) return 0;

    std::tm roundTrip {};
    localtime_r(&converted, &roundTrip);
    std::ostringstream check;
    check << std::put_time(&roundTrip, "%Y-%m-%d %H:%M");
    return check.str() == value ? converted : 0;
}

[[nodiscard]] auto homeDir() -> std::filesystem::path
{
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        throw std::runtime_error("HOME is not set; cannot resolve the schedule directory.");
    }
    return std::filesystem::path { home };
}

[[nodiscard]] auto typeToString(ScheduleType type) -> std::string
{
    switch (type) {
    case ScheduleType::Once: return "once";
    case ScheduleType::Daily: return "daily";
    case ScheduleType::Weekly: return "weekly";
    case ScheduleType::Monthly: return "monthly";
    case ScheduleType::Cron: return "cron";
    }
    return "daily";
}

[[nodiscard]] auto typeFromString(const std::string& value) -> ScheduleType
{
    if (value == "once") return ScheduleType::Once;
    if (value == "weekly") return ScheduleType::Weekly;
    if (value == "monthly") return ScheduleType::Monthly;
    if (value == "cron") return ScheduleType::Cron;
    return ScheduleType::Daily;
}

[[nodiscard]] auto scopeToString(ScheduleScope scope) -> std::string
{
    return scope == ScheduleScope::System ? "system" : "user";
}

[[nodiscard]] auto scopeFromString(const std::string& value) -> ScheduleScope
{
    return value == "system" ? ScheduleScope::System : ScheduleScope::User;
}

using CalendarRow = std::vector<std::pair<std::string, int>>;

[[nodiscard]] auto expandCalendarRows(
    const std::vector<std::pair<std::string, const CronField*>>& dimensions
) -> std::vector<CalendarRow>
{
    std::vector<CalendarRow> rows { {} };
    for (const auto& [name, field] : dimensions) {
        if (field->unrestricted) continue;
        std::vector<CalendarRow> expanded;
        for (const auto& row : rows) {
            for (const int value : field->values) {
                auto next = row;
                next.emplace_back(name, value);
                expanded.push_back(std::move(next));
                if (expanded.size() > 2048) {
                    throw std::runtime_error("Schedule expands to too many launchd calendar intervals.");
                }
            }
        }
        rows = std::move(expanded);
    }
    return rows;
}

[[nodiscard]] auto calendarMatrix(const Schedule& schedule) -> std::vector<CalendarRow>
{
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        if (at == 0) return {};
        std::tm time {};
        localtime_r(&at, &time);
        return { { { "Month", time.tm_mon + 1 }, { "Day", time.tm_mday },
                   { "Hour", time.tm_hour }, { "Minute", time.tm_min } } };
    }

    const auto spec = parseCron(schedule.cron);
    if (!spec.valid) return {};

    const std::vector<std::pair<std::string, const CronField*>> common {
        { "Minute", &spec.minute }, { "Hour", &spec.hour }, { "Month", &spec.month }
    };

    if (!spec.dom.unrestricted && !spec.dow.unrestricted) {
        auto domDimensions = common;
        domDimensions.emplace_back("Day", &spec.dom);
        auto dowDimensions = common;
        dowDimensions.emplace_back("Weekday", &spec.dow);
        auto rows = expandCalendarRows(domDimensions);
        auto dowRows = expandCalendarRows(dowDimensions);
        rows.insert(rows.end(), std::make_move_iterator(dowRows.begin()), std::make_move_iterator(dowRows.end()));
        return rows;
    }

    auto dimensions = common;
    dimensions.emplace_back("Day", &spec.dom);
    dimensions.emplace_back("Weekday", &spec.dow);
    return expandCalendarRows(dimensions);
}

[[nodiscard]] auto systemdAvailable(ICommandRunner& runner) -> bool
{
    return runner.run("command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1 && echo ok")
        .output.find("ok") != std::string::npos;
}

[[nodiscard]] auto runElevated(const std::string& shellCommand) -> CommandResult
{
    LocalCommandRunner runner;
#if defined(VAULTIUM_PLATFORM_MACOS)
    std::string escaped;
    for (const char character : shellCommand) {
        if (character == '\\' || character == '"') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return runner.run("osascript -e 'do shell script \"" + escaped + "\" with administrator privileges' 2>&1");
#elif defined(VAULTIUM_PLATFORM_LINUX)
    if (runner.run("command -v pkexec >/dev/null 2>&1 && echo y").output.find('y') != std::string::npos) {
        return runner.run("pkexec sh -c " + shellQuote(shellCommand) + " 2>&1");
    }
    return runner.run("sudo sh -c " + shellQuote(shellCommand) + " 2>&1");
#else
    static_cast<void>(shellCommand);
    return { 1, {} };
#endif
}

} // namespace

auto isValidScheduleId(const std::string& id) -> bool
{
    if (id.empty() || id.size() > 64) return false;
    return std::ranges::all_of(id, [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-';
    });
}

auto isValidCron(const std::string& expression) -> bool
{
    return parseCron(expression).valid;
}

auto nextCronTime(const std::string& expression, std::time_t from) -> std::time_t
{
    const auto spec = parseCron(expression);
    if (!spec.valid) return 0;

    std::time_t candidate = (from / 60 + 1) * 60;
    constexpr long horizonMinutes = 366L * 5L * 24L * 60L;
    for (long index = 0; index < horizonMinutes; ++index, candidate += 60) {
        std::tm time {};
        localtime_r(&candidate, &time);
        if (matchesCron(spec, time)) return candidate;
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
    if (when == 0) return "—";
    std::tm time {};
    localtime_r(&when, &time);
    std::ostringstream stream;
    stream << std::put_time(&time, "%Y-%m-%d %H:%M");
    return stream.str();
}

auto computeNextRun(const Schedule& schedule, std::time_t from) -> std::string
{
    if (!schedule.enabled) return "—";
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        return at > from ? formatLocalTime(at) : "—";
    }
    const auto next = nextCronTime(schedule.cron, from);
    return next == 0 ? "—" : formatLocalTime(next);
}

auto scheduleSummary(const Schedule& schedule) -> std::string
{
    if (schedule.type == ScheduleType::Once) return "Once on " + schedule.onceAt;
    if (!isValidCron(schedule.cron)) return "Invalid schedule";

    std::vector<std::string> tokens;
    std::stringstream stream { schedule.cron };
    std::string token;
    while (stream >> token) tokens.push_back(token);

    const auto time = [&] {
        try { return twoDigit(std::stoi(tokens[1])) + ":" + twoDigit(std::stoi(tokens[0])); }
        catch (...) { return std::string { "?" }; }
    }();

    switch (schedule.type) {
    case ScheduleType::Daily: return "Every day at " + time;
    case ScheduleType::Weekly: {
        static const std::array<const char*, 7> days { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        try {
            const auto day = static_cast<std::size_t>(std::stoi(tokens[4]) % 7);
            return std::string { "Every " } + days.at(day) + " at " + time;
        } catch (...) { return "Weekly schedule"; }
    }
    case ScheduleType::Monthly: return "Day " + tokens[2] + " of each month at " + time;
    case ScheduleType::Cron: return "Cron: " + schedule.cron;
    case ScheduleType::Once: break;
    }
    return schedule.cron;
}

auto scheduleDirectory() -> std::filesystem::path
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    auto directory = homeDir() / "Library" / "Application Support" / "Vaultium" / "schedules";
#else
    auto directory = homeDir() / ".config" / "vaultium" / "schedules";
#endif
    std::filesystem::create_directories(directory);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    return directory;
}

auto serializeSchedule(const Schedule& schedule) -> std::string
{
    std::ostringstream output;
    output << "ID=" << oneLine(schedule.id) << '\n'
        << "NAME=" << oneLine(schedule.name) << '\n'
        << "BACKUP_TYPE=" << oneLine(schedule.backupType) << '\n'
        << "CONFIG=" << oneLine(schedule.configPath) << '\n'
        << "TYPE=" << typeToString(schedule.type) << '\n'
        << "SCOPE=" << scopeToString(schedule.scope) << '\n'
        << "CRON=" << oneLine(schedule.cron) << '\n'
        << "ONCE_AT=" << oneLine(schedule.onceAt) << '\n'
        << "ENABLED=" << (schedule.enabled ? "true" : "false") << '\n'
        << "LAST_RUN=" << oneLine(schedule.lastRun) << '\n'
        << "NEXT_RUN=" << oneLine(schedule.nextRun) << '\n'
        << "LAST_STATUS=" << oneLine(schedule.lastStatus) << '\n'
        << "LAST_ERROR=" << oneLine(schedule.lastError) << '\n'
        << "CREATED=" << oneLine(schedule.createdAt) << '\n'
        << "UPDATED=" << oneLine(schedule.updatedAt) << '\n';
    return output.str();
}

auto parseSchedule(const std::string& text) -> Schedule
{
    Schedule schedule;
    std::stringstream stream { text };
    std::string line;
    while (std::getline(stream, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "ID") schedule.id = value;
        else if (key == "NAME") schedule.name = value;
        else if (key == "BACKUP_TYPE") schedule.backupType = value;
        else if (key == "CONFIG") schedule.configPath = value;
        else if (key == "TYPE") schedule.type = typeFromString(value);
        else if (key == "SCOPE") schedule.scope = scopeFromString(value);
        else if (key == "CRON") schedule.cron = value;
        else if (key == "ONCE_AT") schedule.onceAt = value;
        else if (key == "ENABLED") schedule.enabled = value == "true";
        else if (key == "LAST_RUN") schedule.lastRun = value;
        else if (key == "NEXT_RUN") schedule.nextRun = value;
        else if (key == "LAST_STATUS") schedule.lastStatus = value;
        else if (key == "LAST_ERROR") schedule.lastError = value;
        else if (key == "CREATED") schedule.createdAt = value;
        else if (key == "UPDATED") schedule.updatedAt = value;
    }
    return schedule;
}

auto loadSchedules() -> std::vector<Schedule>
{
    std::vector<Schedule> result;
    const auto directory = scheduleDirectory();
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".schedule") continue;
        std::ifstream file { entry.path() };
        if (!file) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        auto schedule = parseSchedule(buffer.str());
        if (!isValidScheduleId(schedule.id)) continue;
        if (entry.path().filename() != schedule.id + ".schedule") continue;
        result.push_back(std::move(schedule));
    }
    std::ranges::sort(result, [](const Schedule& left, const Schedule& right) { return left.name < right.name; });
    return result;
}

auto loadSchedule(const std::string& id) -> std::optional<Schedule>
{
    if (!isValidScheduleId(id)) return std::nullopt;
    const auto path = scheduleDirectory() / (id + ".schedule");
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) return std::nullopt;
    std::ifstream file { path };
    if (!file) return std::nullopt;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto schedule = parseSchedule(buffer.str());
    if (schedule.id != id || !isValidScheduleId(schedule.id)) return std::nullopt;
    return schedule;
}

auto saveSchedule(const Schedule& schedule) -> void
{
    if (!isValidScheduleId(schedule.id)) throw std::runtime_error("Unsafe schedule id: " + schedule.id);
    if (schedule.type != ScheduleType::Once && !isValidCron(schedule.cron)) throw std::runtime_error("Invalid schedule cron expression.");
    if (schedule.type == ScheduleType::Once && parseOnceAt(schedule.onceAt) == 0) throw std::runtime_error("Invalid one-time schedule timestamp.");

    const auto path = scheduleDirectory() / (schedule.id + ".schedule");
    const auto temporary = std::filesystem::path { path.string() + ".tmp" };
    {
        std::ofstream file { temporary, std::ios::trunc };
        if (!file) throw std::runtime_error("Could not write schedule: " + path.string());
        file << serializeSchedule(schedule);
        file.close();
        if (!file) throw std::runtime_error("Could not finalize schedule: " + path.string());
    }
    std::filesystem::permissions(temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    std::filesystem::rename(temporary, path);
}

auto removeSchedule(const std::string& id) -> void
{
    if (!isValidScheduleId(id)) throw std::runtime_error("Unsafe schedule id: " + id);
    std::error_code error;
    std::filesystem::remove(scheduleDirectory() / (id + ".schedule"), error);
}

auto generateLaunchdPlist(const Schedule& schedule, const std::string& exePath) -> std::string
{
    if (!isValidScheduleId(schedule.id)) throw std::runtime_error("Unsafe schedule id.");
    const auto rows = calendarMatrix(schedule);
    if (rows.empty()) throw std::runtime_error("Schedule cannot be represented by launchd.");

    std::ostringstream output;
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n<dict>\n"
        << "  <key>Label</key><string>com.vaultium." << xmlEscape(schedule.id) << "</string>\n"
        << "  <key>ProgramArguments</key>\n  <array>\n"
        << "    <string>" << xmlEscape(exePath) << "</string>\n"
        << "    <string>schedule</string>\n    <string>run</string>\n"
        << "    <string>--id</string>\n    <string>" << xmlEscape(schedule.id) << "</string>\n"
        << "  </array>\n"
        << "  <key>StartCalendarInterval</key>\n";

    const auto emitRow = [&output](const CalendarRow& row) {
        output << "    <dict>\n";
        for (const auto& [key, value] : row) {
            output << "      <key>" << key << "</key><integer>" << value << "</integer>\n";
        }
        output << "    </dict>\n";
    };

    if (rows.size() == 1) emitRow(rows.front());
    else {
        output << "  <array>\n";
        for (const auto& row : rows) emitRow(row);
        output << "  </array>\n";
    }

    const auto log = scheduleDirectory() / (schedule.id + ".log");
    output << "  <key>StandardErrorPath</key><string>" << xmlEscape(log.string()) << "</string>\n"
        << "  <key>StandardOutPath</key><string>" << xmlEscape(log.string()) << "</string>\n"
        << "</dict>\n</plist>\n";
    return output.str();
}

auto generateSystemdService(const Schedule& schedule, const std::string& exePath) -> std::string
{
    if (!isValidScheduleId(schedule.id)) throw std::runtime_error("Unsafe schedule id.");
    std::ostringstream output;
    output << "[Unit]\nDescription=Vaultium scheduled backup: " << oneLine(schedule.name) << "\n\n"
        << "[Service]\nType=oneshot\n"
        << "ExecStart=" << systemdQuote(exePath) << " schedule run --id " << schedule.id << "\n";
    return output.str();
}

auto generateSystemdTimer(const Schedule& schedule) -> std::string
{
    std::string onCalendar;
    if (schedule.type == ScheduleType::Once) {
        if (parseOnceAt(schedule.onceAt) == 0) throw std::runtime_error("Invalid one-time schedule.");
        onCalendar = schedule.onceAt + ":00";
    } else {
        std::vector<std::string> tokens;
        std::stringstream stream { schedule.cron };
        std::string token;
        while (stream >> token) tokens.push_back(token);
        if (tokens.size() != 5 || !isValidCron(schedule.cron)) throw std::runtime_error("Invalid cron expression.");

        if (schedule.type == ScheduleType::Cron) {
            throw std::runtime_error("Raw cron expressions use the cron backend on Linux.");
        }

        std::string prefix;
        if (schedule.type == ScheduleType::Weekly) {
            static const std::array<const char*, 7> names { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            const auto day = static_cast<std::size_t>(std::stoi(tokens[4]) % 7);
            prefix = std::string { names.at(day) } + " ";
        }
        const auto month = tokens[3] == "*" ? "*" : tokens[3];
        const auto day = tokens[2] == "*" ? "*" : tokens[2];
        const auto hour = tokens[1].size() == 1 ? "0" + tokens[1] : tokens[1];
        const auto minute = tokens[0].size() == 1 ? "0" + tokens[0] : tokens[0];
        onCalendar = prefix + "*-" + month + "-" + day + " " + hour + ":" + minute + ":00";
    }

    std::ostringstream output;
    output << "[Unit]\nDescription=Vaultium schedule timer: " << oneLine(schedule.name) << "\n\n"
        << "[Timer]\nOnCalendar=" << onCalendar << "\nPersistent=true\n\n"
        << "[Install]\nWantedBy=timers.target\n";
    return output.str();
}

auto generateCrontabLine(const Schedule& schedule, const std::string& exePath) -> std::string
{
    if (!isValidScheduleId(schedule.id)) throw std::runtime_error("Unsafe schedule id.");
    std::string cron = schedule.cron;
    if (schedule.type == ScheduleType::Once) {
        const auto at = parseOnceAt(schedule.onceAt);
        if (at == 0) throw std::runtime_error("Invalid one-time schedule.");
        std::tm time {};
        localtime_r(&at, &time);
        cron = std::to_string(time.tm_min) + " " + std::to_string(time.tm_hour) + " "
            + std::to_string(time.tm_mday) + " " + std::to_string(time.tm_mon + 1) + " *";
    } else if (!isValidCron(cron)) {
        throw std::runtime_error("Invalid cron expression.");
    }
    return cron + " " + shellQuote(exePath) + " schedule run --id " + schedule.id + " # vaultium:" + schedule.id;
}

auto launchdPlistPath(const std::string& id, ScheduleScope scope) -> std::filesystem::path
{
    if (!isValidScheduleId(id)) throw std::runtime_error("Unsafe schedule id: " + id);
    const auto file = "com.vaultium." + id + ".plist";
    if (scope == ScheduleScope::System) return std::filesystem::path { "/Library/LaunchDaemons" } / file;
    return homeDir() / "Library" / "LaunchAgents" / file;
}

auto schedulerBackendName(ScheduleScope scope) -> std::string
{
#if defined(VAULTIUM_PLATFORM_MACOS)
    return scope == ScheduleScope::System ? "launchd (system)" : "launchd";
#elif defined(VAULTIUM_PLATFORM_LINUX)
    if (scope == ScheduleScope::System) return "systemd (system)";
    LocalCommandRunner runner;
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
    if (!isValidScheduleId(id)) return false;
#if defined(VAULTIUM_PLATFORM_MACOS)
    return std::filesystem::exists(launchdPlistPath(id, scope));
#elif defined(VAULTIUM_PLATFORM_LINUX)
    if (scope == ScheduleScope::System) {
        return std::filesystem::exists(std::filesystem::path { "/etc/systemd/system" } / ("vaultium-" + id + ".timer"));
    }
    if (std::filesystem::exists(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".timer"))) return true;
    LocalCommandRunner runner;
    return runner.run("crontab -l 2>/dev/null | grep -F " + shellQuote("vaultium:" + id) + " >/dev/null && echo yes")
        .output.find("yes") != std::string::npos;
#else
    static_cast<void>(scope);
    return false;
#endif
}

auto triggerInstalled(const std::string& id) -> bool
{
    return triggerInstalled(id, ScheduleScope::User) || triggerInstalled(id, ScheduleScope::System);
}

auto installTrigger(const Schedule& schedule, const std::string& exePath) -> TriggerResult
{
    if (!isValidScheduleId(schedule.id)) return { false, "invalid", "Unsafe schedule id." };
#if defined(VAULTIUM_PLATFORM_MACOS)
    const auto plistContent = generateLaunchdPlist(schedule, exePath);
    if (schedule.scope == ScheduleScope::System) {
        const auto temporary = std::filesystem::temp_directory_path() / ("com.vaultium." + schedule.id + ".plist");
        { std::ofstream file { temporary, std::ios::trunc }; file << plistContent; }
        const auto destination = launchdPlistPath(schedule.id, ScheduleScope::System);
        const auto command = "mkdir -p /Library/LaunchDaemons && cp " + shellQuote(temporary.string()) + " "
            + shellQuote(destination.string()) + " && chown root:wheel " + shellQuote(destination.string())
            + " && chmod 644 " + shellQuote(destination.string()) + " && launchctl unload "
            + shellQuote(destination.string()) + " 2>/dev/null || true; launchctl load " + shellQuote(destination.string());
        const auto result = runElevated(command);
        std::error_code error;
        std::filesystem::remove(temporary, error);
        return { result.exitCode == 0, "launchd (system)", result.exitCode == 0 ? "System LaunchDaemon installed." : "Admin install failed or cancelled: " + result.output };
    }

    const auto plist = launchdPlistPath(schedule.id, ScheduleScope::User);
    std::filesystem::create_directories(plist.parent_path());
    { std::ofstream file { plist, std::ios::trunc }; file << plistContent; }
    LocalCommandRunner runner;
    static_cast<void>(runner.run("launchctl unload " + shellQuote(plist.string()) + " 2>/dev/null"));
    const auto load = runner.run("launchctl load " + shellQuote(plist.string()) + " 2>&1");
    return { load.exitCode == 0, "launchd", load.exitCode == 0 ? "LaunchAgent installed." : "launchctl load failed: " + load.output };
#elif defined(VAULTIUM_PLATFORM_LINUX)
    LocalCommandRunner runner;

    if (schedule.type == ScheduleType::Cron) {
        if (schedule.scope == ScheduleScope::System) {
            return { false, "cron", "System-scope raw cron expressions are not installed automatically; use a structured schedule type." };
        }
        const auto line = generateCrontabLine(schedule, exePath);
        const auto marker = "vaultium:" + schedule.id;
        const auto command = "( crontab -l 2>/dev/null | grep -vF " + shellQuote(marker) + "; echo " + shellQuote(line) + " ) | crontab - 2>&1";
        const auto result = runner.run(command);
        return { result.exitCode == 0, "cron", result.exitCode == 0 ? "cron entry installed." : "crontab failed: " + result.output };
    }

    if (schedule.scope == ScheduleScope::System) {
        const auto serviceTemporary = std::filesystem::temp_directory_path() / ("vaultium-" + schedule.id + ".service");
        const auto timerTemporary = std::filesystem::temp_directory_path() / ("vaultium-" + schedule.id + ".timer");
        { std::ofstream { serviceTemporary, std::ios::trunc } << generateSystemdService(schedule, exePath); }
        { std::ofstream { timerTemporary, std::ios::trunc } << generateSystemdTimer(schedule); }
        const auto serviceDestination = "/etc/systemd/system/vaultium-" + schedule.id + ".service";
        const auto timerDestination = "/etc/systemd/system/vaultium-" + schedule.id + ".timer";
        const auto command = "cp " + shellQuote(serviceTemporary.string()) + " " + shellQuote(serviceDestination)
            + " && cp " + shellQuote(timerTemporary.string()) + " " + shellQuote(timerDestination)
            + " && chmod 644 " + shellQuote(serviceDestination) + " " + shellQuote(timerDestination)
            + " && systemctl daemon-reload && systemctl enable --now vaultium-" + schedule.id + ".timer";
        const auto result = runElevated(command);
        std::error_code error;
        std::filesystem::remove(serviceTemporary, error);
        std::filesystem::remove(timerTemporary, error);
        return { result.exitCode == 0, "systemd (system)", result.exitCode == 0 ? "System timer installed." : "Admin install failed or cancelled: " + result.output };
    }

    if (systemdAvailable(runner)) {
        const auto directory = homeDir() / ".config" / "systemd" / "user";
        std::filesystem::create_directories(directory);
        { std::ofstream { directory / ("vaultium-" + schedule.id + ".service"), std::ios::trunc } << generateSystemdService(schedule, exePath); }
        { std::ofstream { directory / ("vaultium-" + schedule.id + ".timer"), std::ios::trunc } << generateSystemdTimer(schedule); }
        static_cast<void>(runner.run("systemctl --user daemon-reload"));
        const auto enable = runner.run("systemctl --user enable --now vaultium-" + schedule.id + ".timer 2>&1");
        return { enable.exitCode == 0, "systemd", enable.exitCode == 0 ? "systemd --user timer installed." : "enable failed: " + enable.output };
    }

    const auto line = generateCrontabLine(schedule, exePath);
    const auto marker = "vaultium:" + schedule.id;
    const auto command = "( crontab -l 2>/dev/null | grep -vF " + shellQuote(marker) + "; echo " + shellQuote(line) + " ) | crontab - 2>&1";
    const auto result = runner.run(command);
    return { result.exitCode == 0, "cron", result.exitCode == 0 ? "cron entry installed." : "crontab failed: " + result.output };
#else
    static_cast<void>(exePath);
    return { false, "unsupported", "Scheduling is not implemented on this platform." };
#endif
}

auto uninstallTrigger(const std::string& id) -> TriggerResult
{
    if (!isValidScheduleId(id)) return { false, "invalid", "Unsafe schedule id." };
#if defined(VAULTIUM_PLATFORM_MACOS)
    LocalCommandRunner runner;
    const auto userPlist = launchdPlistPath(id, ScheduleScope::User);
    static_cast<void>(runner.run("launchctl unload " + shellQuote(userPlist.string()) + " 2>/dev/null"));
    std::error_code error;
    std::filesystem::remove(userPlist, error);
    const auto systemPlist = launchdPlistPath(id, ScheduleScope::System);
    if (std::filesystem::exists(systemPlist)) {
        const auto result = runElevated("launchctl unload " + shellQuote(systemPlist.string()) + " 2>/dev/null || true; rm -f " + shellQuote(systemPlist.string()));
        if (result.exitCode != 0) return { false, "launchd (system)", "Could not remove system trigger: " + result.output };
    }
    return { true, "launchd", "Trigger removed." };
#elif defined(VAULTIUM_PLATFORM_LINUX)
    LocalCommandRunner runner;
    static_cast<void>(runner.run("systemctl --user disable --now vaultium-" + id + ".timer 2>/dev/null"));
    std::error_code error;
    std::filesystem::remove(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".timer"), error);
    std::filesystem::remove(homeDir() / ".config" / "systemd" / "user" / ("vaultium-" + id + ".service"), error);
    static_cast<void>(runner.run("systemctl --user daemon-reload 2>/dev/null"));
    static_cast<void>(runner.run("( crontab -l 2>/dev/null | grep -vF " + shellQuote("vaultium:" + id) + " ) | crontab - 2>/dev/null"));

    const auto systemTimer = std::filesystem::path { "/etc/systemd/system" } / ("vaultium-" + id + ".timer");
    if (std::filesystem::exists(systemTimer)) {
        const auto command = "systemctl disable --now vaultium-" + id + ".timer 2>/dev/null || true; rm -f "
            + shellQuote("/etc/systemd/system/vaultium-" + id + ".timer") + " "
            + shellQuote("/etc/systemd/system/vaultium-" + id + ".service") + "; systemctl daemon-reload";
        const auto result = runElevated(command);
        if (result.exitCode != 0) return { false, "systemd (system)", "Could not remove system trigger: " + result.output };
    }
    return { true, "systemd", "Trigger removed." };
#else
    return { false, "unsupported", "Not implemented." };
#endif
}

} // namespace vaultium
