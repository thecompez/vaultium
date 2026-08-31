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

enum class ScheduleScope {
    User,
    System
};

/**
 * @brief A persisted scheduled backup.
 */
struct Schedule {
    std::string id;
    std::string name;
    std::string backupType;
    std::string configPath;
    ScheduleType type { ScheduleType::Daily };
    ScheduleScope scope { ScheduleScope::User };
    std::string cron;
    std::string onceAt;
    bool enabled { true };
    std::string lastRun;
    std::string nextRun;
    std::string lastStatus;
    std::string lastError;
    std::string createdAt;
    std::string updatedAt;
};

/**
 * @brief Validates a schedule identifier for persistence and OS-trigger use.
 * @param id Identifier to validate.
 * @return true when the identifier is safe.
 */
[[nodiscard]] auto isValidScheduleId(const std::string& id) -> bool;

/**
 * @brief Validates a standard five-field cron expression.
 */
[[nodiscard]] auto isValidCron(const std::string& expr) -> bool;

/**
 * @brief Returns the first matching local time strictly after @p from.
 */
[[nodiscard]] auto nextCronTime(const std::string& expr, std::time_t from) -> std::time_t;

[[nodiscard]] auto cronForDaily(int hour, int minute) -> std::string;
[[nodiscard]] auto cronForWeekly(int dayOfWeek, int hour, int minute) -> std::string;
[[nodiscard]] auto cronForMonthly(int dayOfMonth, int hour, int minute) -> std::string;
[[nodiscard]] auto scheduleSummary(const Schedule& schedule) -> std::string;
[[nodiscard]] auto computeNextRun(const Schedule& schedule, std::time_t from) -> std::string;
[[nodiscard]] auto formatLocalTime(std::time_t when) -> std::string;

[[nodiscard]] auto scheduleDirectory() -> std::filesystem::path;
[[nodiscard]] auto loadSchedules() -> std::vector<Schedule>;
[[nodiscard]] auto loadSchedule(const std::string& id) -> std::optional<Schedule>;
auto saveSchedule(const Schedule& schedule) -> void;
auto removeSchedule(const std::string& id) -> void;
[[nodiscard]] auto serializeSchedule(const Schedule& schedule) -> std::string;
[[nodiscard]] auto parseSchedule(const std::string& text) -> Schedule;

struct TriggerResult {
    bool ok {};
    std::string backend;
    std::string message;
};

[[nodiscard]] auto schedulerBackendName() -> std::string;
[[nodiscard]] auto schedulerBackendName(ScheduleScope scope) -> std::string;
[[nodiscard]] auto generateLaunchdPlist(const Schedule& schedule, const std::string& exePath) -> std::string;
[[nodiscard]] auto generateSystemdService(const Schedule& schedule, const std::string& exePath) -> std::string;
[[nodiscard]] auto generateSystemdTimer(const Schedule& schedule) -> std::string;
[[nodiscard]] auto generateCrontabLine(const Schedule& schedule, const std::string& exePath) -> std::string;
[[nodiscard]] auto launchdPlistPath(const std::string& id, ScheduleScope scope) -> std::filesystem::path;
auto installTrigger(const Schedule& schedule, const std::string& exePath) -> TriggerResult;
auto uninstallTrigger(const std::string& id) -> TriggerResult;
[[nodiscard]] auto triggerInstalled(const std::string& id) -> bool;
[[nodiscard]] auto triggerInstalled(const std::string& id, ScheduleScope scope) -> bool;

} // namespace vaultium
