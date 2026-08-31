# Vaultium

Vaultium is a C++23 backup manager for Linux and macOS. It provides one core engine for local and SSH-based backups, verification, restore, retention, scheduling, CLI automation, and an optional Qt/QML desktop application.

Version **0.3.0** treats backup integrity as part of the operation: a successful local backup is published only after the artifact is complete and, when enabled, its integrity sidecars are written successfully.

## Release support

| Area | Status |
|---|---|
| Linux | Supported |
| macOS | Supported |
| Windows | Not release-ready in 0.3 |
| C++ | C++23 |
| CMake | 3.28+ |
| CLI | Supported |
| Qt/QML GUI | Optional, Qt 6.5+ |
| Local backup | Supported |
| Remote SSH backup | Supported |
| Agent API | Experimental scaffold; not part of the 0.3 production contract |

Vaultium intentionally fails configuration on unsupported platforms instead of presenting partially implemented behavior as cross-platform support.

## Backup sources

### Filesystem

Archives one or more absolute paths with `tar`, optionally compressed with `gzip`.

### Service configuration

Backs up curated configuration trees for supported services such as Nginx, Apache, systemd, Docker, MySQL/MariaDB, PostgreSQL, Redis, MongoDB, Caddy, Fail2Ban, WireGuard, OpenVPN, and Xray. Missing optional paths are recorded rather than silently presented as included.

### MySQL / MariaDB

Uses `mysqldump`/compatible dump tooling. Local jobs may use a `MYSQL_DEFAULTS_FILE`; leaving it empty allows the client to use its normal authentication path, including local socket authentication.

### PostgreSQL

Uses `pg_dump` or `pg_dumpall`. Passwords are read from `POSTGRES_PASSWORD_FILE` and injected only into the spawned database client process. Vaultium does not mutate the parent process environment with `PGPASSWORD`.

### SQLite

Vaultium does **not** copy a live SQLite database file directly. Each configured database is snapshotted with the `sqlite3` online `.backup` command first, then the snapshots are archived. This preserves a consistent database image while WAL or transactions are active.

SQLite files in one job must have unique basenames so restores remain unambiguous.

## Integrity model

For local jobs, Vaultium uses the following lifecycle:

1. acquire the job lock;
2. write the backup to a temporary artifact;
3. validate the source-specific structure;
4. publish to a collision-safe final filename;
5. write a SHA-256 sidecar when `BACKUP_CHECKSUM=true`;
6. write source metadata when available;
7. roll back the published artifact if required sidecar publication fails;
8. apply retention cleanup only after successful publication.

A restore always verifies the artifact first. If checksums are enabled, a missing `.sha256` sidecar is an integrity failure rather than a warning.

## Build

### Dependencies

Required for the headless CLI/core build:

- CMake 3.28+
- Ninja or another generator with C++ module support
- a C++23 compiler with CMake module scanning support
- `pkg-config`
- `libssh2`
- `tar`
- `gzip`
- database client/dump tools required by the selected engine
- `sqlite3` for SQLite jobs

Ubuntu/Debian example:

```bash
sudo apt update
sudo apt install -y clang cmake ninja-build pkg-config libssh2-1-dev sqlite3 gzip tar
```

macOS with Homebrew:

```bash
brew install cmake ninja llvm pkg-config libssh2 sqlite
```

### Headless build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVAULTIUM_BUILD_GUI=OFF \
  -DVAULTIUM_BUILD_TESTS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### GUI build

Qt 6.5 or newer is required.

```bash
cmake -S . -B build-gui -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVAULTIUM_BUILD_GUI=ON

cmake --build build-gui --parallel
```

### Install

```bash
cmake --install build --prefix /usr/local
```

## Configuration

Configuration files use strict `KEY=VALUE` syntax. Unknown keys are rejected so misspelled security or backup settings do not silently fall back to defaults.

Tracked examples are available in [`examples/`](examples/).

### Filesystem example

```ini
BACKUP_ENABLED=true
BACKUP_SOURCE=filesystem
EXECUTION_MODE=local
BACKUP_PATHS=/etc/nginx,/etc/systemd/system
BACKUP_DIR=/var/backups/vaultium
LOCK_FILE=/run/vaultium.lock
BACKUP_COMPRESS=true
BACKUP_VALIDATE_GZIP=true
BACKUP_CHECKSUM=true
BACKUP_CLEANUP_ENABLED=true
BACKUP_RETENTION_DAYS=14
BACKUP_INTERVAL_MINUTES=1440
TAR_PATH=/usr/bin/tar
GZIP_PATH=/usr/bin/gzip
```

### SQLite example

```ini
BACKUP_ENABLED=true
BACKUP_SOURCE=database
BACKUP_ENGINE=sqlite
EXECUTION_MODE=local
SQLITE_FILES=/srv/app/data/app.sqlite
SQLITE3_PATH=/usr/bin/sqlite3
BACKUP_DIR=/var/backups/vaultium
LOCK_FILE=/run/vaultium.lock
BACKUP_COMPRESS=true
BACKUP_VALIDATE_GZIP=true
BACKUP_CHECKSUM=true
TAR_PATH=/usr/bin/tar
GZIP_PATH=/usr/bin/gzip
```

### PostgreSQL example

Store the database password in an owner-readable file rather than in the Vaultium configuration itself:

```bash
sudo install -m 600 /dev/null /etc/vaultium/postgres_password
sudo sh -c 'printf "%s\n" "replace-me" > /etc/vaultium/postgres_password'
```

```ini
BACKUP_SOURCE=database
BACKUP_ENGINE=postgresql
EXECUTION_MODE=local
BACKUP_DATABASES=all
POSTGRES_HOST=127.0.0.1
POSTGRES_PORT=5432
POSTGRES_USER=backup_user
POSTGRES_PASSWORD_FILE=/etc/vaultium/postgres_password
PG_DUMP_PATH=/usr/bin/pg_dump
PG_DUMPALL_PATH=/usr/bin/pg_dumpall
PSQL_PATH=/usr/bin/psql
BACKUP_DIR=/var/backups/vaultium
LOCK_FILE=/run/vaultium.lock
BACKUP_CHECKSUM=true
```

### MySQL / MariaDB example

For file-based credentials, use a client defaults file with restrictive permissions:

```ini
[client]
user=backup_user
password=replace-me
host=127.0.0.1
port=3306
```

```bash
chmod 600 /etc/vaultium/mysql.cnf
```

Then configure:

```ini
BACKUP_SOURCE=database
BACKUP_ENGINE=mysql
EXECUTION_MODE=local
BACKUP_DATABASES=all
MYSQL_DEFAULTS_FILE=/etc/vaultium/mysql.cnf
MYSQLDUMP_PATH=/usr/bin/mysqldump
MYSQL_PATH=/usr/bin/mysql
BACKUP_DIR=/var/backups/vaultium
LOCK_FILE=/run/vaultium.lock
BACKUP_CHECKSUM=true
```

`MYSQL_DEFAULTS_FILE` may be omitted when the local database client is already authenticated through its normal configuration or socket authentication.

## CLI

Run one local backup:

```bash
vaultium backup --config /etc/vaultium/vaultium.conf
```

Run continuously using `BACKUP_INTERVAL_MINUTES`:

```bash
vaultium loop --config /etc/vaultium/vaultium.conf
```

Verify an artifact:

```bash
vaultium verify \
  --config /etc/vaultium/vaultium.conf \
  --archive /var/backups/vaultium/files_2026-08-31_02-00-00.tar.gz
```

Dry-run a restore:

```bash
vaultium restore \
  --config /etc/vaultium/vaultium.conf \
  --archive /var/backups/vaultium/files_2026-08-31_02-00-00.tar.gz \
  --dest /tmp/vaultium-restore \
  --dry-run
```

Apply a restore only after verification and an explicit overwrite confirmation:

```bash
vaultium restore \
  --config /etc/vaultium/vaultium.conf \
  --archive /var/backups/vaultium/files_2026-08-31_02-00-00.tar.gz \
  --dest /srv/restore \
  --overwrite
```

## Remote SSH mode

Remote mode uses libssh2 rather than shelling out to a local `ssh` executable. Strict host-key checking is enabled by default.

Important settings include:

```ini
EXECUTION_MODE=remote_ssh
REMOTE_HOST=backup.example.com
REMOTE_PORT=22
REMOTE_USER=backup
REMOTE_AUTH_METHOD=key
REMOTE_IDENTITY_FILE=/home/user/.ssh/id_ed25519
REMOTE_KNOWN_HOSTS_FILE=/home/user/.ssh/known_hosts
REMOTE_STRICT_HOST_KEY=true
REMOTE_DOWNLOAD_DIR=/var/backups/vaultium/remote
REMOTE_SERVER_BACKUP_DIR=/tmp/vaultium_remote_backups
REMOTE_REMOVE_AFTER_DOWNLOAD=true
REMOTE_CONNECT_TIMEOUT_SECONDS=15
REMOTE_COMMAND_TIMEOUT_SECONDS=600
```

Test connectivity and prerequisites:

```bash
vaultium remote-test --config ./remote.conf
```

Run a remote backup:

```bash
vaultium remote --config ./remote.conf
```

Do not disable strict host-key verification in unattended production jobs.

## Scheduling

Vaultium persists schedule records and installs native OS triggers:

- macOS: launchd
- Linux structured schedules: systemd user/system timers when available
- Linux raw cron expressions: user crontab

Schedule IDs are validated before they are used in filenames, unit names, or shell commands. Schedule records are written atomically with owner-only permissions.

Examples:

```bash
vaultium schedule save \
  --name "Nightly filesystem backup" \
  --type daily \
  --time 02:30 \
  --backup-config /etc/vaultium/filesystem.conf \
  --backup-type filesystem \
  --enabled true

vaultium schedule list
```

System-scope schedules require OS administrator authorization. Raw cron expressions are intentionally not auto-installed as system-scope jobs; use a structured schedule type for that case.

## Security notes

- Keep private keys and database credential files outside the repository.
- Use `0600` for credential files and SSH private keys.
- Keep `REMOTE_STRICT_HOST_KEY=true` in production.
- Treat backup artifacts as sensitive data; Vaultium sets generated local artifacts and sidecars to owner read/write only.
- Restore is destructive. Use `--dry-run` first and require explicit `--overwrite` to apply database/service changes.
- Do not expose the experimental agent scaffold as a network service.

See [`SECURITY.md`](SECURITY.md) for vulnerability reporting and the supported security boundary.

## Tests and CI

Pull requests run a headless Linux/macOS matrix with warnings promoted to errors and execute the CTest suite. The release branch should not be merged while either platform is red.

## License

See [`LICENSE`](LICENSE).
