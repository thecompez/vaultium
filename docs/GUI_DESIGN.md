# Vaultium — GUI Product Design & Architecture

> Status: design baseline (v1). Scope: turn the Qt/QML app from a CLI frontend into a
> guided, commercial-grade backup product. The project binary/module names remain
> `vaultium` / `vaultium_core`; "Vaultium" in the brief is treated as a product
> name synonym.

This document is the deliverable that must exist **before** screens are built:
information architecture, navigation, journeys, screen hierarchy, the component
system, the design-system foundation, the QML architecture, and — critically — the
**core capabilities the GUI depends on but that do not exist yet.**

---

## 1. Product vision & principles

Vaultium lets a non-technical operator protect a server's data without knowing
Linux, SSH, SQL, or systemd. They connect a server once; Vaultium *discovers* what's
on it and presents it as checkboxes; they choose a destination; Vaultium does the
rest and can prove the backup is restorable.

Design principles (used to settle every trade-off):

1. **Discovery over input.** The product inspects the machine and offers choices.
   Typing a path is an advanced escape hatch, never the default.
2. **One primary action per screen.** Each screen has a single obvious next step.
3. **Plain language first.** "Website files", not `/var/www`. "Database", not `mysqldump`.
4. **Safe by default.** Destructive actions are dry-run first, confirmed, and undoable
   where physically possible.
5. **Always explain state.** Loading → skeletons; nothing → empty states with a CTA;
   failure → the real reason + a recovery path.
6. **Two depths, one product.** Simple mode hides everything advanced; Advanced mode
   reveals it in the *same* screens (progressive disclosure), never a separate app.
7. **The core owns the truth.** All inspection, strategy, and execution live in
   `vaultium_core`. QML is presentation + view-models only.

---

## 2. Personas & modes

| Persona | Goal | Mode |
|---|---|---|
| **Operator** (small-business owner, agency dev) | "Back up my site + DB nightly, restore when something breaks." | **Simple** |
| **Administrator** (sysadmin, DevOps) | Control retention, compression, paths, schedules, verification. | **Advanced** |

**Simple mode**: recommended selections pre-checked, advanced fields hidden, plain
labels, defaults chosen by the core's strategy engine.
**Advanced mode**: same flows, plus custom paths, retention rules, compression,
SSH tuning, verification policy, per-table strategy, restore options.

Mode is a global toggle (Settings + header), persisted via `QSettings`. It only
changes *disclosure*, never the navigation map.

---

## 3. Information architecture

Top-level areas (the permanent left sidebar):

```
Dashboard        Overview: health, last/failed runs, storage, schedules
Servers          Connected machines + their discovered inventory (Explorer)
Backups          Start a backup (Wizard) + view/run saved backup plans
Restore          Restore Wizard: browse backups → preview → restore
Schedules        Recurring jobs, next run, enable/disable
Activity         Run history + the live Console/log stream
Settings         Account, destinations, mode (Simple/Advanced), preferences
```

Rationale: this mirrors the *nouns* of the product (servers, backups, restores,
schedules) rather than the CLI's verbs. "Create Job" disappears as a nav item — it
becomes the **Backup Wizard** launched by a primary button from Dashboard, Servers,
and Backups.

---

## 4. Navigation structure

- **Primary nav**: persistent sidebar (the seven areas above), active-state highlight,
  icon + label. Collapsible to icons on narrow widths (≥ adaptive breakpoint).
- **Secondary nav**: in-page tabs where an area has sub-views (e.g. a Server detail
  page has tabs: *Files · Databases · Services*).
- **Wizards** are modal *flows* (full-content overlays with a step rail), not sidebar
  destinations. They trap focus, have Back/Next, and a persistent Cancel with
  unsaved-change confirmation.
- **Global header** (per content area): page title + contextual primary action +
  Simple/Advanced switch + server picker when relevant.
- **Console** docks at the bottom across the app (already implemented) — collapsible,
  auto-expands on error.

Back behavior is predictable; wizard steps preserve state when navigating Back.

---

## 5. User journeys

### J1 — First backup (Simple, the golden path)
1. Dashboard (empty) → **"Protect a server"**.
2. Wizard Step 1 *Server*: pick existing or **Add server** (host, user, auth; Test).
3. On connect, Vaultium **discovers** the machine (skeleton → inventory).
4. Step 2 *What to back up*: three grouped, pre-recommended checklists — **Files**,
   **Databases**, **Services** — with sizes and plain labels.
5. Step 3 *Destination*: "Download to this computer" (default) or a server folder;
   Advanced adds retention/compression.
6. Step 4 *Review*: human summary ("Website files (2.1 GB), database `shop` — nightly,
   keep 7 days, download here"). Est. size + warnings.
7. Step 5 *Run*: live progress + Console; success card with **Verify now** and
   **Schedule this** follow-ups.

### J2 — Restore (Simple)
Restore → pick a backup (grouped by server/date) → **preview contents** → select items →
**Verify** → choose target → dry-run preview of changes → confirm → restore → result.

### J3 — Schedule
From a finished backup or Schedules → "Repeat this backup" → frequency (Daily/Weekly/…)
→ retention → save. Schedules list shows next run + last result.

### J4 — Diagnose a failure
Dashboard failed card → opens the run in Activity → Console shows the colored log with
the real error → **Copy diagnostics** → fix (often a one-click hint, e.g. "enable
password login" or "install mysqldump on the server").

---

## 6. Screen hierarchy

```
App Shell (sidebar + header + docked Console)
├─ Dashboard
│   ├─ Health summary cards (protected servers, last backup, failures, storage)
│   ├─ Recent activity list
│   ├─ Upcoming schedules
│   └─ Empty state → Protect a server (launches Wizard)
├─ Servers
│   ├─ Server list (status, OS, last seen)
│   ├─ Add Server flow (connection + Test + Save)
│   └─ Server detail  ── tabs ──▶  Files | Databases | Services   (the Explorer)
├─ Backups
│   ├─ Backup plans list (run now, edit, duplicate, delete)
│   └─ Backup Wizard (Steps 1–5)
├─ Restore
│   └─ Restore Wizard (Browse → Preview → Select → Verify → Target → Confirm → Result)
├─ Schedules
│   └─ Schedule list + editor
├─ Activity
│   ├─ Run history (filter by server/status/date)
│   └─ Run detail (timeline + full log + artifact + verification)
└─ Settings
    ├─ Mode (Simple/Advanced), Destinations, Notifications, About
    └─ Advanced defaults (retention, compression, verification, SSH)
```

---

## 7. Backup Wizard (detailed)

Five steps with a left step-rail (numbered, current/done states), Back/Next, Cancel.

- **Step 1 — Server.** Choose a connected server or add one. "Add server" sub-form:
  Name, Host, User, Auth (key/password + passphrase), Test connection (live).
  On success → triggers discovery.
- **Step 2 — What to back up.** Three collapsible sections fed by discovery:
  - *Files*: a tree (disks → roots → dirs) with checkboxes, sizes, expand/collapse,
    tri-state parent checks. Recommended dirs pre-checked (e.g. detected web roots).
  - *Databases*: server → database → tables tree, multi-select; "whole database" or
    specific tables; the core derives the dump strategy.
  - *Services*: detected services (Nginx, Docker, …) each expanding to mapped assets
    (config, sites, SSL, compose, volumes) — all selectable, no paths shown.
  - Simple mode shows recommendations + sizes only; Advanced adds "Add custom path".
- **Step 3 — Destination.** "This computer" (download) | "A folder on the server" |
  (future) cloud. Advanced reveals retention, compression, checksum, naming.
- **Step 4 — Review.** Plain-language summary grouped by category, total estimated
  size, and any warnings (missing tool on server, large selection, etc.).
- **Step 5 — Run.** Progress (per-item where possible) + live Console; terminal state
  is a success/failure card with next actions (Verify, Schedule, View in Activity).

The wizard's output is a **Backup Plan** (persisted config + selection manifest) which
also appears under Backups and can be scheduled.

---

## 8. Restore Wizard (detailed)

1. **Browse** backups (grouped by server, newest first; integrity badge).
2. **Preview** contents (read the manifest/metadata sidecar; list files/dbs/services).
3. **Select** items to restore (same tree/checkbox paradigm as backup).
4. **Verify** checksum/structure before touching anything.
5. **Target** (original location | alternate folder | another server).
6. **Confirm** — always a **dry run preview** first (what will change), then an
   explicit, danger-styled confirm for overwrite. Undo where the medium allows
   (e.g. move-aside instead of delete).
7. **Result** with log + verification.

---

## 9. Discovery & Explorer — UX + required core API

This is the heart of the product and the **biggest new engineering work**. The GUI
cannot invent this; `vaultium_core` must provide a read-only inspection service that
runs locally or over the existing SSH client.

### 9.1 UX
- Triggered automatically after a server connects (and re-runnable).
- Shows **loading skeletons** per section while probing.
- Results render as selectable trees with sizes, tri-state checkboxes, search/filter,
  and "Recommended" chips. Failures degrade gracefully (e.g. "Couldn't read sizes —
  showing names only").

### 9.2 Core API contract (new — `vaultium_core` Discovery service)
Read-only, safe probes only. No mutation. Proposed module `vaultium_core_inventory`:

```
struct DiskInfo        { path; fsType; totalBytes; usedBytes; }
struct FsNode          { path; displayName; bytes (optional); isDir; childrenLoaded; }
struct DatabaseInfo    { engine; name; tables[]; sizeBytes (optional); }
struct DbServerInfo    { engine; version; host; databases[]; }
struct ServiceInfo     { id; displayName; present; assets[] (label→paths[]); }
struct Inventory       { disks[]; roots[]; dbServers[]; services[]; warnings[]; }

class InventoryService {
    Inventory probe(const ConnectionTarget&);          // top-level, fast
    std::vector<FsNode> listDir(target, path);          // lazy tree expansion
    DiskUsage dirSize(target, path);                    // optional, async
};
```

Implementation strategy (safe, no shell injection — reuse argv exec / SSH exec):
- **Filesystem**: `lsblk -J` / `df` for disks; `ls`-equivalent for lazy listing;
  `du -sb` (bounded, opt-in) for sizes. Over SSH this runs through the existing
  `SshClient::execute` with shell-quoted args.
- **Databases**: detect binaries (`mysql`, `psql`); enumerate via `SHOW DATABASES`,
  `SHOW TABLES`, `information_schema` sizes; Postgres via `psql -Atc`. Credentials
  reuse the connection/defaults-file model already in config.
- **Services**: a **service catalog** (extends the existing `service_catalog`) maps a
  service id → detection probe (binary/unit/dir exists) → asset path groups
  (config, sites, ssl, compose, volumes). Detection = read-only existence checks.

The catalog is data, not code paths, so new services are added declaratively.

> **Until this exists**, the wizard's discovery panels must either (a) be backed by an
> "agent/probe" build step, or (b) fall back to manual entry clearly labeled as a
> temporary path. We do not ship fake trees.

---

## 10. Dashboard

Cards (visual, scannable):
- **Protection status**: # servers protected, last successful backup (relative time).
- **Failures**: count + the most recent failing job (click → Activity).
- **Storage**: used vs available at destination(s), trend.
- **Schedules**: next run, enabled count.
- **Recent activity**: last N runs with status pills.
- **Verification**: % of recent backups verified.
Empty state (no servers): a single friendly CTA — *Protect a server*.

---

## 11. Logging & diagnostics

Builds on the existing docked Console. Requirements:
- **Real-time** streaming (already: `controller.logMessage`).
- **Severity** colors: INFO (muted), SUCCESS (green), WARNING (amber), ERROR (red).
- **Search** + **filter by level**.
- **Copy** (all / selected lines), **Export** to file, **Clear**.
- Per-**run** logs retained in Activity (not just the live tail).
- A one-click **Copy diagnostics** bundle (env + last run log + redacted config) for
  support.

---

## 12. Design-system foundation

Already implemented in `Theme.qml` (dark + light, slate + green accent, 4/8 spacing,
type scale, radii, motion). Extensions this product needs:

- **Elevation scale** (e.g. `elev0..elev3`) for cards/sheets/popovers (consistent
  shadows), since we add overlays and wizards.
- **Status tokens** already present (success/warning/danger/info) — formalize
  `*Soft`/foreground pairs (done) and add `neutralSoft`.
- **Z-index scale** token set (content < sticky < wizard < dialog < toast).
- **Skeleton** color tokens (base + shimmer).
- **Typography roles** mapped to the scale: Display, Title, Heading, Body, Label,
  Mono (already have sizes; name the roles for consistent use).
- **Density**: comfortable default; Advanced mode may opt into compact tables.

No raw hex in components — everything via `Theme`.

---

## 13. Component system (catalog)

Existing (keep/extend): `Theme`, `AppIcon`, `Card`, `AppButton`, `IconButton`,
`AppTextField`, `AppComboBox`, `AppSwitch`, `StatusPill`, `NavItem`, `Toast`,
`LogConsole`.

New components required (built once, reused everywhere):

| Component | Purpose |
|---|---|
| `WizardShell` | Step rail + header + Back/Next/Cancel + transition between steps |
| `StepRail` | Numbered steps with done/current/upcoming states |
| `CheckTree` / `TreeNode` | Lazy, tri-state, multi-select tree (files, DBs) |
| `SelectableRow` | Checkbox + icon + label + meta (size) + recommended chip |
| `Skeleton` | Shimmer placeholders (lines, cards, rows) |
| `ProgressBar` / `ProgressRing` | Determinate + indeterminate progress |
| `StatCard` | Dashboard metric card (icon, value, label, trend) |
| `EmptyState` | Icon + headline + body + CTA |
| `ConfirmDialog` | Themed modal (variant: info/danger), focus-trapped |
| `Sheet` / `Modal` | Generic overlay with scrim + enter/exit motion |
| `SearchField` | Search input with clear |
| `FilterChips` | Toggleable filter pills (log levels, statuses) |
| `Banner` | Inline contextual messages (warning/error/info) with action |
| `SegmentedControl` | Simple/Advanced, destination choice, etc. |
| `Tabs` | Secondary in-page navigation (server detail) |
| `KeyValueList` | Review-step summaries |
| `Tag` / `Chip` | Recommended, engine type, sizes |
| `Tooltip` | (Controls) standardized usage |

Every component: themed (light/dark), keyboard-navigable, ≥ adequate hit area, with
hover/pressed/disabled/focus states.

---

## 14. QML architecture plan

```
gui/
├─ main.cpp                       Qt entry, QQuickStyle=Basic, registers VM types
├─ app/                           C++ view-models (QObject, QML_ELEMENT)
│   ├─ AppState                   global: mode (Simple/Advanced), current server, nav
│   ├─ ServersViewModel           list/add/connect servers; owns InventoryService
│   ├─ InventoryViewModel         exposes Inventory as QAbstractItemModel trees
│   ├─ BackupPlanViewModel        wizard selection → BackupPlan/config
│   ├─ JobRunner                  wraps engine execution (QProcess/CLI) + progress
│   ├─ RestoreViewModel
│   ├─ ScheduleViewModel
│   ├─ HistoryViewModel           run history + per-run logs
│   └─ LogModel                   live + persisted log lines (filter/search)
└─ qml/
    ├─ Theme.qml (singleton)      design tokens
    ├─ controls/                  the component catalog (§13)
    ├─ AppShell.qml               sidebar + header + Console + page router
    ├─ pages/                     Dashboard, Servers, ServerDetail, Backups,
    │                             Schedules, Activity, Settings
    └─ wizards/                   BackupWizard/, RestoreWizard/ (+ their Step*.qml)
```

Principles:
- **Trees are C++ models.** `InventoryViewModel` exposes `QAbstractItemModel`s so
  large filesystem/DB trees are virtualized (`TreeView`/`ListView` delegates) — never
  thousands of QML items in JS arrays.
- **One source of truth per flow.** The wizard binds to `BackupPlanViewModel`;
  navigating steps never loses state.
- **No business logic in QML.** QML calls view-model slots; view-models call
  `vaultium_core`. Discovery, strategy, execution all in core.
- **Execution** continues via the `QProcess`→CLI pattern (already in place) so
  fork/SSH never run on the GUI thread; `JobRunner` parses progress/log lines.
- **Navigation** is a single `StackView`/`Loader` router driven by `AppState`; wizards
  are pushed as overlays.
- **Persistence**: `QSettings` for mode/prefs; saved servers, plans, schedules in a
  small JSON store under the app config dir (later: core-owned).

---

## 15. Core backend additions required (the honest list)

The GUI is blocked on these `vaultium_core` capabilities; they must be built to make
the product real (not a mock):

1. **InventoryService** (§9.2): filesystem/disk probe + lazy listing + sizes;
   DB/table enumeration; service detection via an extended declarative catalog.
   Runs locally and over the existing SSH client, read-only, injection-safe.
2. **Selection → strategy compiler**: turn a multi-source selection (files + specific
   tables + service asset groups) into one or more backup operations + a manifest.
3. **BackupPlan model + store**: persisted plan (sources, destination, schedule,
   retention) the GUI and CLI share.
4. **Progress reporting**: structured progress events from the engine (currently
   text logs) so the GUI can show real per-item progress, not just a spinner.
5. **Scheduling**: a scheduler (or generate systemd timer / launchd / cron) plus a
   plan registry; status of last/next run.
6. **Restore preview/manifest**: list a backup's contents without extracting; partial
   restore (single db/table/folder); dry-run diff.
7. **Verification surface**: per-run verification result stored with history.

These are sequenced in the roadmap below.

---

## 16. Implementation roadmap (phased, each phase shippable & verifiable)

**Phase 0 — Foundation (GUI-only, no new core).**
New shell with the §3 IA, `AppState` + Simple/Advanced toggle, component catalog
(`WizardShell`, `CheckTree`, `Skeleton`, `EmptyState`, `ConfirmDialog`, `StatCard`,
`SegmentedControl`, `Tabs`, `Banner`), Dashboard with real data we already have,
Activity tab wrapping the Console + run history, and a Backup Wizard **frame** (steps,
review, run) using the *existing* manual config as Step 2's "Advanced/custom" path.

**Phase 1 — Inventory core + Explorer.**
`InventoryService` (filesystem first: disks, lazy tree, opt-in sizes) over local + SSH.
`InventoryViewModel` trees. Wire the wizard's Step 2 Files panel + Server detail Files
tab. Skeletons + graceful degradation.

**Phase 2 — Database & service discovery.**
DB/table enumeration; service catalog detection + asset mapping. Wizard Databases &
Services panels. Selection → strategy compiler.

**Phase 3 — Plans, scheduling, restore preview.**
BackupPlan store; Schedules; Restore Wizard with manifest preview + partial restore +
dry-run diff; structured progress events.

**Phase 4 — Polish & premium.**
Animations/transitions pass, skeleton coverage, diagnostics bundle, notifications,
empty/error-state sweep, accessibility & keyboard pass, light/dark QA.

---

## 17. Acceptance criteria (definition of "professional", per phase)

- A non-technical user completes J1 (first backup) without typing a path or knowing SSH.
- Every async surface has a skeleton; every empty surface has a CTA; every failure
  shows the real cause + a next step.
- No raw hex/spacing in components; all via `Theme`. Light & dark both pass contrast.
- No business logic in QML; trees are virtualized C++ models.
- `qmllint` clean; app loads with no runtime QML errors; headless core build + tests
  stay green; GUI stays optional (`VAULTIUM_BUILD_GUI=OFF` unaffected).
