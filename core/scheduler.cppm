module;

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

export module vaultium_core_scheduler;

export namespace vaultium {

enum class ScheduleType {
    Once,
    Daily,
    Weekly,
    Monthly,
    Cron
};

/// Where a schedule's OS trigger lives.
/// User   = current user, runs while logged in (LaunchAgent / systemd --user / user cron).
/// System = all users, runs even when nobody is logged in (LaunchDaemon / systemd
///          system timer / root cron). Installing/removing requires admin rights.
enum class ScheduleScope {
    User,
    System
};

/**
 * @brief A persisted scheduled backup.
 *
 * The source selection / destination / server live in the referenced backup
 * config file (configPath); the schedule record holds scheduling + status state.
 */
struct Schedule {
    std::string id;
    std::string name;
    std::string backupType;        // filesystem | database | service-config | mixed
    std::string configPath;        // backup config this schedule runs
    ScheduleType type { ScheduleType::Daily };
    ScheduleScope scope { ScheduleScope::User };
    std::string cron;              // 5-field cron (Daily/Weekly/Monthly are stored as cron too)
    std::string onceAt;            // "YYYY-MM-DD HH:MM" for one-shot schedules
    bool enabled { true };
    std::string lastRun;
    std::string nextRun;
    std::string lastStatus;        // pending | working | completed | failed | cancelled
    std::string lastError;
    std::string createdAt;
    std::string updatedAt;
};

// -- Cron + next-run -----------------------------------------------------------

/// Validates a standard 5-field cron expression (min hour dom month dow).
[[nodiscard]] auto isValidCron(const std::string& expr) -> bool;

/// First matching time strictly after `from` for a cron expression (local time).
/// Returns 0 if no match within a one-year horizon.
[[nodiscard]] auto nextCronTime(const std::string& expr, std::time_t from) -> std::time_t;

[[nodiscard]] auto cronForDaily(int hour, int minute) -> std::string;
[[nodiscard]] auto cronForWeekly(int dayOfWeek, int hour, int minute) -> std::string;
[[nodiscard]] auto cronForMonthly(int dayOfMonth, int hour, int minute) -> std::string;

/// Human summary, e.g. "Every day at 02:30".
[[nodiscard]] auto scheduleSummary(const Schedule& schedule) -> std::string;

/// Next run as a "YYYY-MM-DD HH:MM" string ("—" if none / disabled / past one-shot).
[[nodiscard]] auto computeNextRun(const Schedule& schedule, std::time_t from) -> std::string;

/// Formats a time_t as local "YYYY-MM-DD HH:MM".
[[nodiscard]] auto formatLocalTime(std::time_t when) -> std::string;

// -- Persistence ---------------------------------------------------------------

/// Directory where schedule records live (created on demand).
[[nodiscard]] auto scheduleDirectory() -> std::filesystem::path;

[[nodiscard]] auto loadSchedules() -> std::vector<Schedule>;
[[nodiscard]] auto loadSchedule(const std::string& id) -> std::optional<Schedule>;
auto saveSchedule(const Schedule& schedule) -> void;
auto removeSchedule(const std::string& id) -> void;

/// Serializes/parses a single schedule record (KEY=VALUE), exposed for testing.
[[nodiscard]] auto serializeSchedule(const Schedule& schedule) -> std::string;
[[nodiscard]] auto parseSchedule(const std::string& text) -> Schedule;

// -- OS trigger generation + install ------------------------------------------

struct TriggerResult {
    bool ok {};
    std::string backend;   // launchd | systemd | cron | unsupported
    std::string message;
};

/// Backend name for a scope on this platform (launchd / systemd / cron / unsupported).
[[nodiscard]] auto schedulerBackendName() -> std::string;
[[nodiscard]] auto schedulerBackendName(ScheduleScope scope) -> std::string;

/// Pure generators (testable without touching the OS).
[[nodiscard]] auto generateLaunchdPlist(const Schedule& schedule, const std::string& exePath) -> std::string;
[[nodiscard]] auto generateSystemdService(const Schedule& schedule, const std::string& exePath) -> std::string;
[[nodiscard]] auto generateSystemdTimer(const Schedule& schedule) -> std::string;
[[nodiscard]] auto generateCrontabLine(const Schedule& schedule, const std::string& exePath) -> std::string;

/// Absolute path of the launchd plist for a schedule (scope-dependent location).
[[nodiscard]] auto launchdPlistPath(const std::string& id, ScheduleScope scope) -> std::filesystem::path;

/// Installs / removes / checks the OS trigger for a schedule (honoring scope).
/// System scope triggers an OS admin-authentication prompt.
auto installTrigger(const Schedule& schedule, const std::string& exePath) -> TriggerResult;
auto uninstallTrigger(const std::string& id) -> TriggerResult;
[[nodiscard]] auto triggerInstalled(const std::string& id) -> bool;
[[nodiscard]] auto triggerInstalled(const std::string& id, ScheduleScope scope) -> bool;

} // namespace vaultium
