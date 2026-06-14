# Vaultium — Delivery status

Snapshot of the requested improvement areas. Build passes (headless + GUI),
`ctest` green, `qmllint` clean (0 errors), no runtime QML errors (verified via the
offscreen render), and file / database / scheduler flows work.

## Done

| # | Item | Notes |
|---|------|-------|
| 1 | **Download progress** | Real byte-level progress from the SFTP loop (`@@PROGRESS` events): file name, total/downloaded size, %, speed, ETA, phase. Indeterminate sweep when no total. Bar hides on completion. |
| 1 | **Cancellation** | `cancel()` kills the child process, cleans up, sets `cancelled` state. Reliable. |
| 2 | **Status consistency** | Unified states (pending/working/completed/failed/cancelled); "Run" is an action, not a status. |
| 3 | **Toolbar alignment** | All title+action headers right-aligned via explicit spacer + vertical centering. Verified by rendering. |
| 4 | **Open / Reveal / Copy path** | In history rows + success notifications (`openPath`, `revealInFolder`, `copyToClipboard`). |
| 5 | **Remote DB backup** | `mariadb-dump` → `mysqldump` auto-detection; no hard-coded `mysql.cnf` (empty default → socket auth); `pipefail` so a failed dump can't masquerade as success. |
| 6 | **Backup history management** | Rename, Export metadata (JSON), Delete (confirm), Open, Reveal, Copy path, Restore. |
| 7 | **Scheduler** | Real OS scheduler: launchd / systemd `--user` / cron, with **System scope** (LaunchDaemon / system timer, admin-elevated) — "set as system default". Verified end-to-end: launchd fired a scheduled backup with the GUI closed, the record updated, disable/delete removed the trigger, broken triggers are detected + repairable. Tests cover cron/next-run/persistence/trigger generation. |
| 8 | **Selection visuals** | Selected rows get accent background + accent border + check, visible without hover. |
| 9 | **File browser navigation** | Breadcrumb (clickable, scrollable), Back / Forward (history), Up, Refresh (single-dir cache invalidation), double-click to open, strong selected state, folder sizes. |
| 10 | **Application + DB inclusion** | Detected apps show an "include database" choice (default on); selecting an app with a DB auto-includes it. |
| 11 | **Service discovery** | Expanded catalog (nginx, apache, systemd, docker, mysql/mariadb, postgresql, redis, mongodb, caddy, fail2ban, wireguard, openvpn, xray, …) with config-path mapping. |
| 12 | **Wizard redesign** | Modern vertical step indicator: per-step icons, active/completed/upcoming states, connectors, per-step descriptions, smooth content fade between steps. |
| 12 | **Custom tooltip** | All default Qt tooltips replaced by the themed `IconButton.tip` tooltip, applied app-wide. |
| 13 | **UI polish** | Themed empty / loading (skeleton) / error (banner) / success (toast) states; consistent typography/spacing via `Theme`; light + dark. |

## Intentionally deferred (with reason)

- **Download pause/resume** — libssh2 SFTP has no clean pause primitive; a real
  implementation means aborting + resuming at an offset (separate feature). Cancel
  is implemented; pause is out.
- **History "Re-run"** — a standalone artifact doesn't carry its source config;
  re-running a backup is done from **Backups → Run** (jobs do carry their config).
- **Database → schema → table trees in the wizard** — needs interactive credential
  capture + a live DB to verify; the core enumeration hooks exist, the GUI shows
  engine-level selection ("all databases") and points to Manual setup for creds.
- **Windows scheduler** — clean abstraction returns a clear "not implemented yet"
  status rather than pretending; launchd/systemd/cron are real.
- **System-scope scheduler interactive verification** — the install path and
  generation are unit-tested; the actual elevated install shows an OS admin prompt,
  so it's validated interactively, not in CI.
