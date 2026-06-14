// Vaultium test suite.
//
// Dependency-free: a tiny assertion harness drives tests against the
// vaultium_core library modules. Run via `ctest` or by executing the binary.

#include <chrono>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

import vaultium_core;

namespace {

int g_checks {};
int g_failures {};

auto check(bool condition, const std::string& what) -> void
{
    ++g_checks;

    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

template <typename Fn>
auto expectThrow(Fn&& fn, const std::string& what) -> void
{
    ++g_checks;

    try {
        fn();
        ++g_failures;
        std::printf("  FAIL: expected exception: %s\n", what.c_str());
    } catch (const std::exception&) {
        // expected
    }
}

auto runTest(const std::string& name, const std::function<void()>& body) -> void
{
    const auto before = g_failures;
    std::printf("[ RUN ] %s\n", name.c_str());

    try {
        body();
    } catch (const std::exception& exception) {
        ++g_failures;
        std::printf("  FAIL: unexpected exception: %s\n", exception.what());
    }

    std::printf("%s %s\n", g_failures == before ? "[ OK  ]" : "[FAIL ]", name.c_str());
}

// Unique temp directory per test run.
auto makeTempDir(const std::string& tag) -> std::filesystem::path
{
    const auto base = std::filesystem::temp_directory_path()
        / ("vaultium_test_" + tag + "_" + std::to_string(::getpid()) + "_" + std::to_string(std::rand()));
    std::filesystem::create_directories(base);
    return base;
}

auto writeFile(const std::filesystem::path& path, const std::string& content) -> void
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file { path, std::ios::trunc };
    file << content;
}

auto readFile(const std::filesystem::path& path) -> std::string
{
    std::ifstream file { path, std::ios::binary };
    return std::string { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
}

auto countBackups(const std::filesystem::path& dir) -> int
{
    int count {};

    if (!std::filesystem::exists(dir)) {
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("files_") && !name.ends_with(".sha256") && !name.ends_with(".tmp")) {
            ++count;
        }
    }

    return count;
}

// ---------------------------------------------------------------------------

auto testSha256() -> void
{
    using namespace vaultium;

    check(sha256Hex(std::string {}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 of empty string");
    check(sha256Hex(std::string { "abc" }) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 of \"abc\"");

    const auto dir = makeTempDir("sha");
    const auto file = dir / "data.txt";
    writeFile(file, "abc");
    check(sha256File(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256File matches string hash");

    std::filesystem::remove_all(dir);
}

auto testConfigParsing() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("cfg");
    const auto target = dir / "payload.txt";
    writeFile(target, "hello");

    const auto confPath = dir / "fs.conf";
    writeFile(confPath,
        "BACKUP_SOURCE=filesystem\n"
        "EXECUTION_MODE=local\n"
        "BACKUP_COMPRESS=true\n"
        "BACKUP_CHECKSUM=false\n"
        "BACKUP_PATHS=" + target.string() + "\n"
        "BACKUP_DIR=" + (dir / "out").string() + "\n"
        "TAR_PATH=/usr/bin/tar\n"
        "GZIP_PATH=/usr/bin/gzip\n");

    const auto config = loadBackupConfig(confPath);
    check(config.sourceType == SourceType::Filesystem, "BACKUP_SOURCE parsed as filesystem");
    check(config.compress, "BACKUP_COMPRESS parsed true");
    check(!config.checksumEnabled, "BACKUP_CHECKSUM parsed false");
    check(config.backupPaths.size() == 1, "BACKUP_PATHS parsed one path");

    // Unknown keys must be rejected, not silently ignored.
    const auto badPath = dir / "bad.conf";
    writeFile(badPath,
        "BACKUP_SOURCE=filesystem\n"
        "EXECUTION_MODE=local\n"
        "BACKUP_PATHS=" + target.string() + "\n"
        "BACKUP_DIR=" + (dir / "out").string() + "\n"
        "TAR_PATH=/usr/bin/tar\n"
        "DEFINITELY_NOT_A_KEY=1\n");
    expectThrow([&] { (void) loadBackupConfig(badPath); }, "unknown key rejected");

    std::filesystem::remove_all(dir);
}

// Regression test for the parser fix: keys that were previously declared in the
// config struct but never parsed (silently dropped) must now take effect.
auto testRemoteKeysAreParsed() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("remote");
    const auto identity = dir / "id_key";
    writeFile(identity, "dummy-key");

    const auto confPath = dir / "remote.conf";
    writeFile(confPath,
        "BACKUP_SOURCE=database\n"
        "EXECUTION_MODE=remote_ssh\n"
        "BACKUP_ENGINE=mysql\n"
        "REMOTE_HOST=db.example.com\n"
        "REMOTE_USER=backup\n"
        "REMOTE_AUTH_METHOD=key\n"
        "REMOTE_IDENTITY_FILE=" + identity.string() + "\n"
        "REMOTE_KNOWN_HOSTS_FILE=" + (dir / "known_hosts").string() + "\n"
        "REMOTE_STRICT_HOST_KEY=false\n"
        "REMOTE_DOWNLOAD_DIR=" + (dir / "dl").string() + "\n"
        "REMOTE_SERVER_BACKUP_DIR=/home/backup/vaultium\n"
        "REMOTE_REMOVE_AFTER_DOWNLOAD=false\n"
        "POSTGRES_HOST=10.0.0.5\n"
        "POSTGRES_PORT=6543\n");

    const auto config = loadBackupConfig(confPath);
    check(config.remoteServerBackupDirectory == "/home/backup/vaultium",
        "REMOTE_SERVER_BACKUP_DIR is parsed (was previously dropped)");
    check(!config.remoteRemoveAfterDownload, "REMOTE_REMOVE_AFTER_DOWNLOAD parsed false");
    check(!config.strictHostKey, "REMOTE_STRICT_HOST_KEY parsed false");
    check(config.postgresHost == "10.0.0.5", "POSTGRES_HOST parsed");
    check(config.postgresPort == "6543", "POSTGRES_PORT parsed");

    std::filesystem::remove_all(dir);
}

auto testArtifactNaming() -> void
{
    using namespace vaultium;

    DatabaseBackupSource db;
    FilesystemBackupSource fs;

    BackupConfig mysqlFull;
    mysqlFull.engineType = BackupEngineType::MySql;
    mysqlFull.databaseMode = DatabaseMode::All;
    mysqlFull.compress = true;
    check(db.artifactPrefix(mysqlFull) == "mysql_full_", "mysql full prefix");
    check(db.artifactExtension(mysqlFull) == ".sql.gz", "mysql compressed extension");

    BackupConfig mysqlPlain = mysqlFull;
    mysqlPlain.compress = false;
    check(db.artifactExtension(mysqlPlain) == ".sql", "mysql uncompressed extension");

    BackupConfig mysqlSelected = mysqlFull;
    mysqlSelected.databaseMode = DatabaseMode::Selected;
    check(db.artifactPrefix(mysqlSelected) == "mysql_selected_", "mysql selected prefix");

    BackupConfig sqlite;
    sqlite.engineType = BackupEngineType::Sqlite;
    sqlite.compress = true;
    check(db.artifactPrefix(sqlite) == "sqlite_files_", "sqlite prefix");
    check(db.artifactExtension(sqlite) == ".tar.gz", "sqlite compressed extension");

    BackupConfig filesystem;
    filesystem.sourceType = SourceType::Filesystem;
    filesystem.compress = true;
    check(fs.artifactPrefix(filesystem) == "files_", "filesystem prefix");
    check(fs.artifactExtension(filesystem) == ".tar.gz", "filesystem compressed extension");
    filesystem.compress = false;
    check(fs.artifactExtension(filesystem) == ".tar", "filesystem uncompressed extension");
}

auto testFilesystemBackupRestoreVerify() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("fsbackup");
    const auto srcDir = dir / "src";
    const auto srcFile = srcDir / "note.txt";
    writeFile(srcFile, "vaultium filesystem payload");

    BackupConfig config;
    config.sourceType = SourceType::Filesystem;
    config.compress = true;
    config.validateGzip = true;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.backupPaths = { srcDir };

    FilesystemBackupSource source;

    const auto archive = dir / "backup.tar.gz";
    const auto artifact = source.createBackup(config, archive);
    check(std::filesystem::exists(artifact.path), "filesystem archive created");
    check(artifact.size > 0, "filesystem archive non-empty");

    check(source.verify(config, archive), "filesystem archive verifies");

    const auto destDir = dir / "restored";
    RestoreOptions options;
    options.destination = destDir;
    options.overwrite = true;
    source.restore(config, archive, options);

    // The archive stores paths relative to the filesystem root, so the file
    // reappears under destDir + the source's relative path.
    const auto restored = destDir / srcFile.relative_path();
    check(std::filesystem::exists(restored), "restored file exists");
    check(readFile(restored) == "vaultium filesystem payload", "restored content matches");

    std::filesystem::remove_all(dir);
}

auto testLockingSkipsBackup() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("lock");
    const auto srcFile = dir / "data.txt";
    writeFile(srcFile, "lock test");

    BackupConfig config;
    config.sourceType = SourceType::Filesystem;
    config.compress = true;
    config.checksumEnabled = false;
    config.cleanupEnabled = false;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.backupPaths = { srcFile };
    config.backupDirectory = dir / "out";
    config.lockFile = dir / "vaultium.lock";

    // Hold the lock exactly as BackupManager would, then confirm a run is skipped.
    const int fd = ::open(config.lockFile.c_str(), O_CREAT | O_RDWR, 0600);
    check(fd != -1, "opened lock file");
    check(::flock(fd, LOCK_EX | LOCK_NB) == 0, "acquired exclusive lock");

    BackupManager manager { config };
    manager.runOnce();
    check(countBackups(config.backupDirectory) == 0, "backup skipped while lock held");

    ::flock(fd, LOCK_UN);
    ::close(fd);

    std::filesystem::remove_all(dir);
}

auto testRetentionCleanup() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("retain");
    const auto srcFile = dir / "data.txt";
    writeFile(srcFile, "retention test");

    const auto outDir = dir / "out";
    std::filesystem::create_directories(outDir);

    // Seed an old backup (and its checksum sidecar) older than the retention age.
    const auto oldBackup = outDir / "files_2000-01-01_00-00-00.tar.gz";
    writeFile(oldBackup, "old");
    writeFile(std::filesystem::path { oldBackup.string() + ".sha256" }, "deadbeef  files_old\n");

    const auto threeDaysAgo = std::filesystem::file_time_type::clock::now() - std::chrono::hours { 72 };
    std::filesystem::last_write_time(oldBackup, threeDaysAgo);

    BackupConfig config;
    config.sourceType = SourceType::Filesystem;
    config.compress = true;
    config.checksumEnabled = true;
    config.cleanupEnabled = true;
    config.retentionDays = 1;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.backupPaths = { srcFile };
    config.backupDirectory = outDir;
    config.lockFile = dir / "vaultium.lock";

    BackupManager manager { config };
    manager.runOnce();

    check(!std::filesystem::exists(oldBackup), "old backup removed by retention");
    check(!std::filesystem::exists(std::filesystem::path { oldBackup.string() + ".sha256" }),
        "old checksum sidecar removed with its backup");
    check(countBackups(outDir) == 1, "exactly one fresh backup remains");

    // The fresh backup must have a checksum sidecar.
    for (const auto& entry : std::filesystem::directory_iterator(outDir)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("files_") && name.ends_with(".tar.gz")) {
            check(std::filesystem::exists(std::filesystem::path { entry.path().string() + ".sha256" }),
                "fresh backup has a checksum sidecar");
        }
    }

    std::filesystem::remove_all(dir);
}

auto testServiceConfigBackup() -> void
{
    using namespace vaultium;

    // Mock an /etc tree under a sandbox: nginx + systemd present, apache absent.
    const auto dir = makeTempDir("service");
    const auto sandbox = dir / "sandbox";
    writeFile(sandbox / "etc/nginx/nginx.conf", "worker_processes auto;");
    writeFile(sandbox / "etc/nginx/sites-available/default", "server {}");
    writeFile(sandbox / "etc/systemd/system/vaultium.service", "[Unit]\n");

    BackupConfig config;
    config.sourceType = SourceType::ServiceConfig;
    config.compress = true;
    config.validateGzip = true;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.services = { "nginx", "apache", "systemd" };
    config.serviceRootPrefix = sandbox;

    ServiceConfigBackupSource source;
    check(source.type() == SourceType::ServiceConfig, "service source reports ServiceConfig type");
    check(source.artifactExtension(config) == ".tar.gz", "service compressed extension");

    const auto archive = dir / "service.tar.gz";
    const auto artifact = source.createBackup(config, archive);

    check(std::filesystem::exists(artifact.path), "service archive created");
    check(artifact.size > 0, "service archive non-empty");
    check(artifact.metadata.present, "service artifact carries metadata");
    check(artifact.metadata.detail == "nginx,apache,systemd", "metadata records canonical services");

    // nginx (/etc/nginx) and systemd (/etc/systemd/system) exist; apache does not.
    bool nginxIncluded {};
    bool systemdIncluded {};
    for (const auto& path : artifact.metadata.includedPaths) {
        if (path.ends_with("/etc/nginx")) {
            nginxIncluded = true;
        }
        if (path.ends_with("/etc/systemd/system")) {
            systemdIncluded = true;
        }
    }
    check(nginxIncluded, "nginx path included");
    check(systemdIncluded, "systemd path included");

    bool apacheSkipped {};
    for (const auto& path : artifact.metadata.skippedPaths) {
        if (path.ends_with("/etc/apache2") || path.ends_with("/etc/httpd")) {
            apacheSkipped = true;
        }
    }
    check(apacheSkipped, "missing apache paths recorded as skipped");

    check(source.verify(config, archive), "service archive verifies");

    std::filesystem::remove_all(dir);
}

auto testServiceConfigFailsWhenAllMissing() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("service_empty");
    const auto sandbox = dir / "empty"; // nothing under it
    std::filesystem::create_directories(sandbox);

    BackupConfig config;
    config.sourceType = SourceType::ServiceConfig;
    config.compress = true;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.services = { "nginx" };
    config.serviceRootPrefix = sandbox;

    ServiceConfigBackupSource source;
    expectThrow(
        [&] { (void) source.createBackup(config, dir / "x.tar.gz"); },
        "service backup fails when all paths are missing");

    std::filesystem::remove_all(dir);
}

auto testServiceConfigRestoreDefaultsToDryRun() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("service_restore");
    const auto sandbox = dir / "sandbox";
    writeFile(sandbox / "etc/nginx/nginx.conf", "config body");

    BackupConfig config;
    config.sourceType = SourceType::ServiceConfig;
    config.compress = true;
    config.tarPath = "/usr/bin/tar";
    config.gzipPath = "/usr/bin/gzip";
    config.services = { "nginx" };
    config.serviceRootPrefix = sandbox;

    ServiceConfigBackupSource source;
    const auto archive = dir / "svc.tar.gz";
    (void) source.createBackup(config, archive);

    // No overwrite, no dry-run -> must behave as a dry run (extract nothing).
    const auto destDir = dir / "restored";
    RestoreOptions options;
    options.destination = destDir;
    source.restore(config, archive, options);
    check(!std::filesystem::exists(destDir) || std::filesystem::is_empty(destDir),
        "service restore without overwrite is a dry run (no files written)");

    // With overwrite, files are actually restored.
    RestoreOptions applied;
    applied.destination = destDir;
    applied.overwrite = true;
    source.restore(config, archive, applied);
    bool restoredSomething {};
    if (std::filesystem::exists(destDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(destDir)) {
            if (entry.is_regular_file()) {
                restoredSomething = true;
            }
        }
    }
    check(restoredSomething, "service restore with overwrite writes files");

    std::filesystem::remove_all(dir);
}

auto testDatabaseSqliteRestore() -> void
{
    using namespace vaultium;

    const auto dir = makeTempDir("dbrestore");
    const auto srcDir = dir / "data";
    writeFile(srcDir / "app.sqlite", "SQLITE-DB-BYTES");

    // Build a tar.gz the same way a SQLite backup would be packaged.
    BackupConfig fsConfig;
    fsConfig.sourceType = SourceType::Filesystem;
    fsConfig.compress = true;
    fsConfig.tarPath = "/usr/bin/tar";
    fsConfig.gzipPath = "/usr/bin/gzip";
    fsConfig.backupPaths = { srcDir };

    FilesystemBackupSource fs;
    const auto archive = dir / "sqlite_files.tar.gz";
    (void) fs.createBackup(fsConfig, archive);

    BackupConfig dbConfig;
    dbConfig.sourceType = SourceType::Database;
    dbConfig.engineType = BackupEngineType::Sqlite;
    dbConfig.compress = true;
    dbConfig.tarPath = "/usr/bin/tar";
    dbConfig.gzipPath = "/usr/bin/gzip";

    DatabaseBackupSource db;

    // Default (no overwrite) is a dry run: nothing extracted.
    const auto destDir = dir / "restored";
    RestoreOptions dryRun;
    dryRun.destination = destDir;
    db.restore(dbConfig, archive, dryRun);
    check(!std::filesystem::exists(destDir) || std::filesystem::is_empty(destDir),
        "sqlite restore default is a dry run");

    // With overwrite (explicit confirm) the files are extracted.
    RestoreOptions applied;
    applied.destination = destDir;
    applied.overwrite = true;
    db.restore(dbConfig, archive, applied);

    bool found {};
    if (std::filesystem::exists(destDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(destDir)) {
            if (entry.is_regular_file() && entry.path().filename() == "app.sqlite") {
                found = true;
                check(readFile(entry.path()) == "SQLITE-DB-BYTES", "restored sqlite content matches");
            }
        }
    }
    check(found, "sqlite restore with overwrite extracts files");

    std::filesystem::remove_all(dir);
}

auto testInventoryLocalDiscovery() -> void
{
    using namespace vaultium;

    // Build a known directory tree.
    const auto dir = std::filesystem::temp_directory_path() / "vaultium_inv_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    { std::ofstream { dir / "file_a.txt" } << "a"; }
    { std::ofstream { dir / "sub" / "nested.txt" } << "b"; }

    LocalCommandRunner runner;
    InventoryService inventory { runner };

    // Directory listing: directories first, names + types correct.
    const auto nodes = inventory.listDirectory(dir.string());
    bool sawDir = false;
    bool sawFile = false;
    for (const auto& node : nodes) {
        if (node.name == "sub") { sawDir = true; check(node.isDir, "sub is a directory"); }
        if (node.name == "file_a.txt") { sawFile = true; check(!node.isDir, "file_a.txt is a file"); }
        check(node.path.starts_with(dir.string()), "child path is under parent");
    }
    check(sawDir, "listDirectory found the subdirectory");
    check(sawFile, "listDirectory found the file");
    if (!nodes.empty()) {
        check(nodes.front().isDir, "directories are listed before files");
    }

    // Disks: at least the root filesystem with non-zero capacity.
    const auto disks = inventory.disks();
    check(!disks.empty(), "disks() returns at least one filesystem");
    bool anyNonZero = false;
    for (const auto& disk : disks) {
        if (disk.totalBytes > 0) { anyNonZero = true; }
    }
    check(anyNonZero, "a filesystem reports non-zero capacity");

    // Services probe runs without throwing and returns the full catalog.
    const auto services = inventory.detectServices();
    check(services.size() == knownServiceNames().size(), "detectServices covers the catalog");

    // Path size is non-zero for a directory holding a real file.
    { std::ofstream { dir / "payload.bin" } << std::string(2048, 'x'); }
    check(inventory.pathSize(dir.string()) > 0, "pathSize reports a non-zero size");

    std::filesystem::remove_all(dir);
}

auto testApplicationDiscovery() -> void
{
    using namespace vaultium;

    const auto root = std::filesystem::temp_directory_path() / "vaultium_apps_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "blog");
    std::filesystem::create_directories(root / "api");
    std::filesystem::create_directories(root / "stack");
    { std::ofstream { root / "blog" / "wp-config.php" } << "<?php"; }
    { std::ofstream { root / "api" / "package.json" } << "{}"; }
    { std::ofstream { root / "stack" / "docker-compose.yml" } << "services:"; }

    LocalCommandRunner runner;
    InventoryService inventory { runner };

    const auto apps = inventory.detectApplications({ root.string() });

    const auto hasType = [&apps](const std::string& type) {
        for (const auto& a : apps) {
            if (a.type == type) { return true; }
        }
        return false;
    };

    check(hasType("wordpress"), "detects WordPress by wp-config.php");
    check(hasType("nodejs"), "detects Node.js by package.json");
    check(hasType("docker"), "detects Docker stack by compose file");

    for (const auto& a : apps) {
        if (a.type == "wordpress") {
            check(a.usesDatabase, "WordPress is flagged as using a database");
            check(!a.paths.empty(), "WordPress has a backup path");
        }
    }

    std::filesystem::remove_all(root);
}

auto testScheduler() -> void
{
    using namespace vaultium;

    // Cron validity.
    check(isValidCron("30 2 * * *"), "valid daily cron");
    check(isValidCron("*/15 * * * *"), "valid step cron");
    check(isValidCron("0 9 * * 1-5"), "valid weekday-range cron");
    check(!isValidCron("99 2 * * *"), "rejects out-of-range minute");
    check(!isValidCron("30 2 * *"), "rejects 4-field cron");
    check(!isValidCron("a b c d e"), "rejects non-numeric cron");

    // Builders.
    check(cronForDaily(2, 30) == "30 2 * * *", "cronForDaily");
    check(cronForWeekly(1, 9, 0) == "0 9 * * 1", "cronForWeekly");
    check(cronForMonthly(15, 3, 5) == "5 3 15 * *", "cronForMonthly");

    // Next-run calculation: 01:00 -> next 02:30 same day.
    std::tm base {};
    base.tm_year = 2026 - 1900; base.tm_mon = 5; base.tm_mday = 10;
    base.tm_hour = 1; base.tm_min = 0; base.tm_isdst = -1;
    const auto from = std::mktime(&base);
    check(formatLocalTime(nextCronTime("30 2 * * *", from)) == "2026-06-10 02:30", "next daily run is today 02:30");
    // 03:00 -> next 02:30 is the following day.
    base.tm_hour = 3;
    const auto from2 = std::mktime(&base);
    check(formatLocalTime(nextCronTime("30 2 * * *", from2)) == "2026-06-11 02:30", "next daily run rolls to tomorrow");

    // Serialize / parse round-trip.
    Schedule s;
    s.id = "test_sched_unit"; s.name = "Unit"; s.backupType = "filesystem";
    s.configPath = "/tmp/x.conf"; s.type = ScheduleType::Daily; s.cron = "30 2 * * *";
    s.enabled = true; s.lastStatus = "completed";
    const auto reparsed = parseSchedule(serializeSchedule(s));
    check(reparsed.id == s.id && reparsed.name == s.name, "serialize/parse keeps id+name");
    check(reparsed.cron == s.cron && reparsed.type == ScheduleType::Daily, "serialize/parse keeps cron+type");
    check(reparsed.enabled && reparsed.lastStatus == "completed", "serialize/parse keeps enabled+status");

    // Persistence round-trip (cleans up after itself).
    saveSchedule(s);
    const auto loaded = loadSchedule("test_sched_unit");
    check(loaded.has_value() && loaded->name == "Unit", "saveSchedule/loadSchedule round-trip");
    removeSchedule("test_sched_unit");
    check(!loadSchedule("test_sched_unit").has_value(), "removeSchedule deletes the record");

    // Trigger generation (pure).
    const auto plist = generateLaunchdPlist(s, "/usr/local/bin/vaultium");
    check(plist.find("com.vaultium.test_sched_unit") != std::string::npos, "plist has the label");
    check(plist.find("<key>Minute</key><integer>30</integer>") != std::string::npos, "plist has Minute 30");
    check(plist.find("<key>Hour</key><integer>2</integer>") != std::string::npos, "plist has Hour 2");
    check(plist.find("<string>run</string>") != std::string::npos, "plist invokes schedule run");

    const auto timer = generateSystemdTimer(s);
    check(timer.find("OnCalendar=") != std::string::npos, "systemd timer has OnCalendar");
    const auto crontab = generateCrontabLine(s, "/usr/local/bin/vaultium");
    check(crontab.find("30 2 * * *") != std::string::npos && crontab.find("vaultium:test_sched_unit") != std::string::npos,
          "crontab line has cron + marker");

    // Scope: serialize/parse + trigger location.
    s.scope = ScheduleScope::System;
    const auto sysReparsed = parseSchedule(serializeSchedule(s));
    check(sysReparsed.scope == ScheduleScope::System, "serialize/parse keeps system scope");
    const auto userReparsed = parseSchedule(serializeSchedule(Schedule { .id = "u", .scope = ScheduleScope::User }));
    check(userReparsed.scope == ScheduleScope::User, "default/user scope round-trips");

    const auto sysPath = launchdPlistPath("test_sched_unit", ScheduleScope::System).string();
    const auto userPath = launchdPlistPath("test_sched_unit", ScheduleScope::User).string();
    check(sysPath == "/Library/LaunchDaemons/com.vaultium.test_sched_unit.plist", "system trigger lives in LaunchDaemons");
    check(userPath.find("/Library/LaunchAgents/com.vaultium.test_sched_unit.plist") != std::string::npos, "user trigger lives in LaunchAgents");
    check(schedulerBackendName(ScheduleScope::System).find("system") != std::string::npos, "system backend name reflects scope");
}

} // namespace

auto main() -> int
{
    runTest("sha256", testSha256);
    runTest("config_parsing", testConfigParsing);
    runTest("remote_keys_parsed", testRemoteKeysAreParsed);
    runTest("artifact_naming", testArtifactNaming);
    runTest("filesystem_backup_restore_verify", testFilesystemBackupRestoreVerify);
    runTest("locking_skips_backup", testLockingSkipsBackup);
    runTest("retention_cleanup", testRetentionCleanup);
    runTest("service_config_backup", testServiceConfigBackup);
    runTest("service_config_fails_when_all_missing", testServiceConfigFailsWhenAllMissing);
    runTest("service_config_restore_dry_run_default", testServiceConfigRestoreDefaultsToDryRun);
    runTest("database_sqlite_restore", testDatabaseSqliteRestore);
    runTest("inventory_local_discovery", testInventoryLocalDiscovery);
    runTest("application_discovery", testApplicationDiscovery);
    runTest("scheduler", testScheduler);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
