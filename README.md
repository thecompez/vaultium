# Vaultium

A cross-platform **backup platform** written in modern C++23 (using C++ modules).

Vaultium started as a database backup tool and has grown into a general,
**source-based** backup manager. A *backup source* is anything Vaultium can back
up, restore, and verify through one uniform interface:

| Source          | What it backs up                                                        | Status |
|-----------------|-------------------------------------------------------------------------|--------|
| `database`      | MySQL/MariaDB, PostgreSQL, SQLite dumps                                  | ✅ backup, ✅ restore |
| `filesystem`    | Arbitrary files and directories                                         | ✅ backup, ✅ restore |
| `service-config`| Curated service config trees (nginx, apache, systemd, docker, mysql, postgresql) | ✅ backup, ✅ restore |

Every source supports **compression**, **retention cleanup**, **SHA-256 integrity
verification**, and **restore**. Backups can run **locally** or **agentlessly over
SSH** (libssh2, with host-key verification).

The core engine (`vaultium_core`) is UI-independent: both the CLI and the desktop Qt/QML GUI consume the same library.

---

## Architecture

```
vaultium_core  (static library, UI-independent)
├── IBackupSource                  interface: backup / restore / verify
│   ├── DatabaseBackupSource       wraps MySQL / PostgreSQL / SQLite engines
│   ├── FilesystemBackupSource     tar(+gzip) of arbitrary paths
│   └── ServiceConfigBackupSource  curated service paths over the filesystem source
├── BackupManager                  source-agnostic lifecycle, retention, checksums, metadata
├── process_runner                 safe argv-based exec (no shell), pipes for gzip
├── sha256                         dependency-free integrity hashing
├── remote/                        libssh2 SSH + SFTP, host-key verification
└── scheduler/                     launchd/systemd timer/cron OS trigger generators & installers

vaultium        CLI front end  (links vaultium_core)
vaultium_gui    Qt/QML front end (optional, VAULTIUM_BUILD_GUI=ON)
vaultium_tests  test suite     (links vaultium_core)
```

### Safety properties

- **No shell execution** for local backups/restores — processes are launched with
  explicit `argv` arrays via `fork`/`execv`. Remote SSH commands are constructed
  with strict single-quote shell-escaping.
- **Integrity**: each artifact gets a `.sha256` sidecar; `verify` checks the
  checksum *and* the archive structure; restore refuses to proceed on mismatch.
- **Host-key verification** for SSH (known_hosts); mismatches always abort.
- **Destructive operations are opt-in**: database and service-config restores
  default to a **dry run** and require an explicit `--overwrite` to apply.

---

## Building

Requires a C++23 compiler with module-dependency scanning, CMake ≥ 3.28, Ninja,
and libssh2.

> **macOS note:** Apple Clang's module scanning isn't picked up by CMake here.
> Use Homebrew LLVM Clang:
>
> ```sh
> brew install llvm ninja cmake libssh2
> cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
> cmake --build build
> ```

Run the tests:

```sh
ctest --test-dir build --output-on-failure
```

### CMake options

| Option                 | Default | Description                                  |
|------------------------|---------|----------------------------------------------|
| `VAULTIUM_BUILD_TESTS` | `ON`    | Build the `vaultium_tests` target            |
| `VAULTIUM_BUILD_GUI`   | `OFF`   | Build the Qt/QML GUI (`vaultium_gui`)        |
| `VAULTIUM_BUILD_UI`    | `OFF`   | Reserved legacy flag                         |

The project builds fully **headless** with `VAULTIUM_BUILD_GUI=OFF` (the default) —
no Qt dependency is required for the CLI or the core library.

---

## CLI usage

```
vaultium backup   --config <conf>                      Run one backup
vaultium loop     --config <conf>                      Run backups on an interval
vaultium verify   --config <conf> --archive <file>     Verify checksum + structure
vaultium restore  --config <conf> --archive <file> --dest <dir> [--overwrite] [--dry-run]
vaultium remote        --config <conf>                 Agentless backup over SSH
vaultium remote-test   --config <conf>                 Test SSH connectivity / tools
vaultium remote-provision --config <conf>              Prepare a remote server
vaultium agent    --config <conf>                      (scaffold) run local loop
vaultium inspect  --config <conf> [options]            Inspect server environment & status
vaultium schedule <action> [options]                   Manage OS-integrated schedules
```

Restore notes:

- **Filesystem**: extracts the archive to `--dest`. Refuses a non-empty
  destination unless `--overwrite`.
- **Service-config / Database**: defaults to a **dry run** that prints what it
  would do; pass `--overwrite` to actually apply the (destructive) restore.

Example configs live in [`examples/`](examples/). Copy one to your config path and
edit it — **never commit real secrets** (the `.gitignore` only tracks
`*.conf.example`).

### Probing with `vaultium inspect`
The `inspect` subcommand probes a server (either local or remote, based on the execution mode in the configuration file) and outputs details in structured JSON.

Usage:
```sh
vaultium inspect --config <conf> --what <what> [--path <path>] [--session]
```

Options:
- `--what <what>`: The type of inventory probe to run. Supported values:
  - `dir`: Lists contents of the directory specified by `--path` (default probe).
  - `disks`: Probes and returns disk usage statistics (total, used, and available bytes).
  - `services`: Probes service presence (nginx, docker, systemd, postgresql, etc.) and lists their configuration paths.
  - `dbengines`: Probes for installed database engines (mysql, postgresql, sqlite).
  - `apps`: Discovers web applications (e.g., WordPress, Drupal) and associated directories.
  - `size`: Returns the recursive directory size of the path specified by `--path`.
  - `databases`: Enumerate databases for a specific engine specified by `--path`.
  - `tables`: Enumerate tables for a database (format: `--path "<engine> <database>"`).
- `--path <path>`: The target path or argument for the probe. Defaults to `/`.
- `--session`: Starts an interactive session on `stdin`, reading commands (e.g., `dir /var/www`) and outputting JSON lines without reconnecting.

### Managing Schedules with `vaultium schedule`
The `schedule` command registers scheduled backups directly into the host OS scheduler (`launchd` on macOS, `systemd` user/system timers on Linux, or `cron` on other UNIX platforms).

Usage:
```sh
vaultium schedule <action> [options]
```

Actions:
- `list`: Lists all schedules as JSON.
- `status`: Checks if OS scheduler triggers are supported on the current platform.
- `save`: Saves or updates a schedule.
- `remove`: Deletes a schedule and uninstalls its OS trigger. Requires `--id <id>`.
- `set-enabled`: Enables or disables a schedule. Requires `--id <id>` and `--enabled <true|false>`.
- `repair`: Re-installs/fixes the OS trigger configuration for a schedule. Requires `--id <id>`.
- `run`: Instantly runs a scheduled backup (used internally by OS triggers). Requires `--id <id>`.

Options for `save`:
- `--id <id>`: Specifies the ID of the schedule. If not provided, a unique ID is auto-generated.
- `--name <name>`: Desired display name for the schedule.
- `--backup-config <path>`: Path to the `.conf` backup configuration file that the schedule executes.
- `--backup-type <type>`: Type of backup being scheduled (`filesystem` | `database` | `service-config` | `mixed`).
- `--enabled <true|false>`: Enable or disable the schedule upon saving.
- `--scope <user|system>`: Specifies the OS scheduler scope. `user` (default) installs the trigger for the current user. `system` installs it system-wide (requires root/admin permissions).
- `--type <type>`: Frequency type (`once` | `daily` | `weekly` | `monthly` | `cron`).
- `--time <HH:MM>`: Trigger time of day (defaults to `02:00`).
- `--dow <0-6>`: Day of the week for weekly schedules (0 = Sunday).
- `--dom <1-31>`: Day of the month for monthly schedules.
- `--once <YYYY-MM-DD HH:MM>`: Date and time for a one-time schedule.
- `--cron <cron_expr>`: Raw 5-field cron expression (for type `cron`).

---

## Configuration reference

Vaultium configurations are stored in key-value format. Typos or unknown keys will fail loudly at load time.

### Common options

| Key                       | Description                                              | Default / Format |
|---------------------------|----------------------------------------------------------|------------------|
| `BACKUP_ENABLED`          | Enable or disable the backup plan.                       | `true` \| `false` (default: `true`) |
| `BACKUP_SOURCE`           | Source type to back up.                                  | `database` \| `filesystem` \| `service-config` |
| `EXECUTION_MODE`          | Where and how the backup is executed.                    | `local` \| `remote_ssh` \| `agent` |
| `BACKUP_DIR`              | Directory where backup archives are written.             | Absolute path |
| `LOCK_FILE`               | Advisory lock file path to prevent concurrent runs.       | Absolute path |
| `BACKUP_COMPRESS`         | Enable gzip compression of the backup archive.           | `true` \| `false` |
| `BACKUP_VALIDATE_GZIP`    | Verify gzip integrity after compression.                 | `true` \| `false` |
| `BACKUP_CHECKSUM`         | Generate a `.sha256` integrity checksum file.             | `true` \| `false` |
| `BACKUP_CLEANUP_ENABLED`  | Enable retention-based cleanup of old archives.           | `true` \| `false` |
| `BACKUP_RETENTION_DAYS`   | Number of days to keep backup archives.                  | Positive integer |
| `BACKUP_INTERVAL_MINUTES` | Frequency of runs when running in `loop` mode.           | Positive integer |
| `GZIP_PATH`               | Path to the `gzip` executable.                            | e.g. `/usr/bin/gzip` |

### Filesystem source options

| Key                       | Description                                              | Format |
|---------------------------|----------------------------------------------------------|--------|
| `BACKUP_PATHS`            | Comma-separated list of directories/files to back up.    | Comma-separated absolute paths |
| `TAR_PATH`                | Path to the `tar` executable.                            | e.g. `/usr/bin/tar` |

### Service-config source options

| Key                          | Description                                              | Format |
|------------------------------|----------------------------------------------------------|--------|
| `BACKUP_SERVICES`            | Curated services to back up (config, sites, SSL, compose).| Comma-separated (e.g. `nginx,systemd,docker,mysql`) |
| `BACKUP_SERVICE_EXTRA_PATHS` | Custom config paths (e.g., Docker Compose files, .env).  | Comma-separated absolute paths |
| `BACKUP_SERVICE_ROOT_PREFIX` | Alternate root path (chroot/sandbox directory prefix).   | Absolute path |
| `TAR_PATH`                   | Path to the `tar` executable.                            | e.g. `/usr/bin/tar` |

### Database source options

| Key                       | Description                                              | Format |
|---------------------------|----------------------------------------------------------|--------|
| `BACKUP_ENGINE`           | Database type to back up.                                | `mysql` (or `mariadb`) \| `postgresql` \| `sqlite` |
| `BACKUP_DATABASES`        | Databases to dump. Use `all` or list specific names.     | `all` \| Comma-separated names (MySQL/PG) |
| `SQLITE_FILES`            | SQLite database file paths to copy.                      | Comma-separated absolute paths (SQLite only) |
| `MYSQLDUMP_PATH`          | Path to the `mysqldump` executable.                      | e.g. `/usr/bin/mysqldump` |
| `MYSQL_PATH`              | Path to the `mysql` client CLI executable.               | e.g. `/usr/bin/mysql` |
| `MYSQL_DEFAULTS_FILE`     | Connection details configuration file (my.cnf format).   | Absolute path (replaces inline passwords) |
| `MYSQL_HOST`              | Hostname of the MySQL database server.                   | Hostname / IP (remote-provision only) |
| `MYSQL_PORT`              | Port of the MySQL database server.                       | Port number (remote-provision only) |
| `MYSQL_USER`              | MySQL user with backup permissions.                      | Username (remote-provision only) |
| `MYSQL_PASSWORD`          | MySQL password for the user.                             | Password string (remote-provision only) |
| `PG_DUMP_PATH`            | Path to the `pg_dump` executable.                        | e.g. `/usr/bin/pg_dump` |
| `PG_DUMPALL_PATH`         | Path to the `pg_dumpall` executable (required for `all`).| e.g. `/usr/bin/pg_dumpall` |
| `PSQL_PATH`               | Path to the `psql` PostgreSQL CLI client.                | e.g. `/usr/bin/psql` |
| `POSTGRES_HOST`           | Hostname of the PostgreSQL server.                       | Hostname / IP |
| `POSTGRES_PORT`           | Port of the PostgreSQL server.                           | Port number |
| `POSTGRES_USER`           | PostgreSQL user with backup permissions.                 | Username |
| `POSTGRES_PASSWORD_FILE`  | Path to pgpass file or file containing PG password.       | Absolute path |

### Remote SSH options (Agentless)

| Key                            | Description                                              | Format |
|--------------------------------|----------------------------------------------------------|--------|
| `REMOTE_HOST`                  | Remote server hostname or IP address.                    | Hostname / IP |
| `REMOTE_USER`                  | SSH username on the remote server.                       | Username |
| `REMOTE_PORT`                  | SSH port on the remote server.                           | Port number (default: `22`) |
| `REMOTE_AUTH_METHOD`           | SSH authentication method.                               | `key` \| `password` |
| `REMOTE_IDENTITY_FILE`         | SSH private key file path.                               | Absolute path (when `key` auth is used) |
| `REMOTE_IDENTITY_PASSPHRASE`   | Passphrase for the SSH private key.                      | Passphrase string |
| `REMOTE_PASSWORD`              | SSH password.                                            | Password string (when `password` auth is used) |
| `REMOTE_KNOWN_HOSTS_FILE`      | Known hosts file for verifying host key.                 | Absolute path |
| `REMOTE_STRICT_HOST_KEY`       | Abort if host key doesn't match `known_hosts`.           | `true` \| `false` |
| `REMOTE_DOWNLOAD_DIR`          | Directory on local client where remote files are saved.  | Absolute path |
| `REMOTE_SERVER_BACKUP_DIR`     | Intermediate directory on remote host for dumps/archives.| Absolute path |
| `REMOTE_REMOVE_AFTER_DOWNLOAD` | Delete the archive from remote server after downloading. | `true` \| `false` |
| `REMOTE_PROVISION_ENABLED`     | Automatically set up directories/credentials on remote.  | `true` \| `false` |
| `REMOTE_PROVISION_CONFIG_DIR`  | Target provisioning config directory on remote host.     | Absolute path |
| `REMOTE_CONNECT_TIMEOUT_SECONDS`| Timeout for SSH handshake and authentication.           | Positive integer |
| `REMOTE_COMMAND_TIMEOUT_SECONDS`| Timeout for remote process execution.                   | Positive integer |

---

## GUI

Vaultium provides a modern, commercial-grade desktop GUI (`vaultium_gui`) built with Qt 6 and QML. The GUI links against `vaultium_core` and interacts with the backup engine through a series of C++ view-models, keeping the UI entirely decoupled from the core logic.

### Key GUI features

- **Interactive File Browser**: Navigate server paths using interactive breadcrumbs, directory caching, and folder sizes.
- **5-Step Backup Wizard**: A guided wizard step indicator covering Server selection, Target/What detection, Destination config, Review, and Run.
- **Server Discovery**: Skeletons load as the server is probed dynamically for disk usage, databases, tables, and services.
- **Backup History Management**: Manage past archives directly from the UI with support for JSON metadata export, log console review, renaming, and one-click restores.
- **Visual Schedulers**: Schedule backups through a GUI linked directly to native OS schedulers (launchd / systemd / cron).
- **Aesthetic UI/UX**: Includes light and dark modes, smooth transitions, skeleton placeholders, banner notifications, custom tooltips, and toast warnings styled using a cohesive design tokens library.

### C++ View-Models
The UI communicates with the core library through these registered view-model classes:
- [BackupController](file:///Users/compez/Documents/GitHub/vaultium/gui/backup_controller.h): Coordinates backup runs, cancels running jobs, and tails logs.
- [BackupJobsModel](file:///Users/compez/Documents/GitHub/vaultium/gui/backup_jobs_model.h): List and manage active or historical backup jobs.
- [InventoryViewModel](file:///Users/compez/Documents/GitHub/vaultium/gui/inventory_view_model.h): Virtualizes large files/databases/services trees for performant QML rendering.
- [ScheduleViewModel](file:///Users/compez/Documents/GitHub/vaultium/gui/schedule_view_model.h): Bridges GUI schedule editing to native OS scheduler installations.
- [ServersViewModel](file:///Users/compez/Documents/GitHub/vaultium/gui/servers_view_model.h): Manages connected servers list and invokes remote connections.

### Building the GUI

Build the GUI by pointing CMake at your Qt 6 installation (Requires Qt Quick, Qt Quick Controls 2, and Qt 6.5+):

```sh
cmake -S . -B build-gui -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos-arm64 \
  -DVAULTIUM_BUILD_GUI=ON
cmake --build build-gui
```

Notes:
- The QML module URI is `VaultiumUI` (not `Vaultium`) so its generated module
  directory does not collide with the `vaultium` executable on case-insensitive
  filesystems (macOS APFS).
- Qt disables C++20 module scanning on its targets, so the GUI target re-enables
  it (`CXX_SCAN_FOR_MODULES ON`) to let the bridge `import vaultium_core`.

---

## Status & roadmap

- ✅ Database / filesystem / service-config backup, restore, and verify
- ✅ Local and agentless-SSH execution (filesystem and databases over SSH)
- ✅ SHA-256 integrity checks, retention cleanups, metadata sidecars
- ✅ Native OS scheduler integrations (launchd, systemd, cron) for User and System scopes
- ✅ Fully-featured Qt/QML GUI (Wizard, file explorer, active scheduler, live console, light/dark modes)
- 🚧 Windows Task Scheduler support (currently returns "not implemented yet")
- 🔜 Service-config over SSH
- 🔜 Direct cloud backups (S3 / SFTP target endpoints)

## License

See repository.
