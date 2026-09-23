# nyst (Not Your Systemd Tree) — systemd dependency tree TUI

You are scaffolding and implementing a C++ terminal application on the user's Arch Linux system. This document is your full brief: the goal, the stack, the architecture, the behaviour, the coding rules, and the order of work. Read all of it before writing any code.

---

## 1. Goal

A TUI that shows **every systemd unit on the machine as a navigable dependency tree**, so the user can see at a glance what might be causing problems. For each unit it must be clear:

- **Is it running?** Active, inactive, failed, or transitioning.
- **Who manages it?** The system manager (root) or the user manager, and for system services, which user it runs as.
- **Where did it come from?** A systemd default, an installed package (and which one), created by the admin, created by the user, generated or transient, or missing.
- **How is it related to other units?** Requires, wants, binds-to, part-of, and triggers.

The user must be able to search, filter by unit type and by the categories above, flip the tree direction, read the unit's journal, and start, stop, restart, reload, enable, or disable units.

The name is `nyst` (binary, project folder, and namespace).

---

## 2. Hard rules

1. **No automated tests.** Do not write unit tests, test targets, or test frameworks. The user tests manually.
2. **Verification means compiling.** After each milestone, the project must configure and build with zero errors and zero warnings under `-Wall -Wextra -Wpedantic`. Do not launch the TUI yourself.
3. **Don't install packages yourself.** If dependencies are missing, list the exact `pacman` command and ask the user to run it.
4. **Don't touch system state.** Never start, stop, enable, or disable units while developing. Never run anything with `sudo`.
5. **Stop after each milestone** and give a short report: what was done, which files changed, and anything the user should check manually.

---

## 3. Clean-code principles (apply everywhere)

The project must be easy to maintain, read, extend, and debug. **When in doubt, choose simplicity over efficiency**, both in logic and in syntax.

- **One responsibility per file.** Each module does one thing, and its header states it in one line.
- **Plain code.** Prefer plain structs, free functions, `std::string`, `std::vector`, and `std::map` or `std::set`. Avoid template metaprogramming, clever lambdas, operator overloading tricks, deep inheritance, and macros (beyond include guards or `#pragma once`).
- **Small functions.** Aim for under ~40 lines per function. When a function grows, extract a well-named helper.
- **Descriptive names.** `loadUnitsFromManager`, not `load2`. No abbreviations beyond the obvious (`dbus`, `ui`, `pkg`).
- **Enums with `to_string`.** Every `enum class` has a `toString()` function next to it, for display and debug output.
- **Comments:**
  - Each file starts with a one-line comment stating its purpose.
  - Each public function and struct in headers gets a short doc comment (`///`) if its purpose isn't obvious from its name.
  - Comments explain **why**, never **what**. No line-by-line narration and no commented-out code.
- **Error handling:**
  - Library exceptions (e.g. `sdbus::Error`) are caught in the `source/` layer and converted into plain data: an `error` string on the unit, or a status message. The UI layer never sees a library exception.
  - A failure on one unit must never abort loading the rest.
- **No global mutable state** except the debug logger.
- **Formatting:** C++20. Add a `.clang-format` based on LLVM with a 4-space indent and a 100-column limit, and keep the code consistent with it.

---

## 4. Stack

| Concern | Library | Notes |
|---|---|---|
| UI | **FTXUI** | Use the system package if `pacman -Si ftxui` finds it; otherwise use CMake `FetchContent` pinned to the latest release tag (verify the tag exists). |
| D-Bus (reading units) | **sdbus-c++** (`extra/sdbus-cpp`) | Check the installed major version (`pacman -Q sdbus-cpp`) and write against **that** version's API. v2 uses strong types like `sdbus::ServiceName`; read the installed headers rather than guessing. |
| Package ownership | **libalpm** (ships with `pacman`) | Use `alpm_initialize("/", "/var/lib/pacman", &err)` and the local DB. |
| Journal | the `journalctl` binary via `popen` | Simpler and matches `journalctl -u` semantics exactly. Isolate it in one module so it can be swapped for `sd-journal` later. |
| Actions | the `systemctl` binary | Lets polkit prompt in the terminal (see §9). |
| Build | CMake ≥ 3.20 + `pkg-config` | |

Expected dependencies: `cmake gcc pkgconf sdbus-cpp pacman`, plus `ftxui` if packaged. Check with `pacman -Q` and report anything missing.

---

## 5. Project layout

```
nyst/
├── CMakeLists.txt
├── .clang-format
├── README.md              # build steps, keybindings, --dump usage
└── src/
    ├── main.cpp           # arg parsing: --dump or TUI
    ├── util/
    │   └── debug_log.hpp/.cpp   # optional file logger (see §11)
    ├── model/
    │   ├── unit.hpp/.cpp        # Unit struct + enums + toString()
    │   └── unit_graph.hpp/.cpp  # all units, forward/reverse edges, lookups
    ├── source/
    │   ├── systemd_bus.hpp/.cpp # read units from system + user managers
    │   ├── package_db.hpp/.cpp  # file path → owning package (libalpm)
    │   ├── classifier.hpp/.cpp  # decide Origin / flags for each unit
    │   ├── loader.hpp/.cpp      # orchestrates the above → UnitGraph
    │   ├── journal.hpp/.cpp     # fetch log lines for a unit
    │   └── actions.hpp/.cpp     # run systemctl commands
    └── ui/
        ├── app.hpp/.cpp         # layout, key handling, screen lifecycle
        ├── tree_view.hpp/.cpp   # expansion state, flattening, row rendering
        ├── filters.hpp/.cpp     # filter state + matching + filter panel
        ├── details_pane.hpp/.cpp
        └── journal_pane.hpp/.cpp
```

Dependency direction: `ui → model ← source`. The `model/` layer depends on nothing but the standard library, and `source/` doesn't know `ui/` exists.

---

## 6. Data model (`model/`)

```cpp
enum class Manager      { System, User };
enum class ActiveState  { Active, Inactive, Failed, Activating, Deactivating, Reloading, Unknown };
enum class Origin       { SystemdDefault, Package, AdminCreated, UserCreated,
                          Generated, Transient, Unowned, Missing, Unknown };
enum class EdgeKind     { Requires, Requisite, Wants, BindsTo, PartOf, Upholds, Triggers };

struct Edge {
    std::string target;   // unit key, see below
    EdgeKind kind;
};

struct Unit {
    std::string key;            // "<manager>:<name>", e.g. "system:sshd.service"; unique
    std::string name;           // "sshd.service"
    std::string type;           // "service", "timer", ... (suffix of name)
    Manager manager;
    std::string description;
    std::string loadState;      // loaded, not-found, masked, error, ...
    ActiveState activeState;
    std::string subState;       // running, exited, dead, ...
    std::string unitFileState;  // enabled, disabled, static, ...
    std::string runAsUser;      // system services: User= or "root"; user units: current user
    Origin origin;
    std::string package;        // owning pacman package, empty if none
    std::string fragmentPath;
    std::string sourcePath;     // e.g. /etc/fstab for generated mounts
    std::vector<std::string> dropInPaths;
    bool locallyModified;       // has a drop-in outside /usr/lib
    bool shadowsPackagedUnit;   // file in /etc with same name as one in /usr/lib
    bool isLoaded;              // false = only known from ListUnitFiles
    std::vector<Edge> dependencies;  // forward edges
    std::string error;          // non-empty if reading this unit partially failed
};
```

`UnitGraph` holds a `std::map<std::string, Unit>` keyed by `Unit::key`, plus a **reverse edge map** it computes itself from the forward edges (don't use systemd's `RequiredBy`/`WantedBy` properties; one source of truth is simpler). It provides simple lookups: `find(key)`, `dependenciesOf(key)`, `dependentsOf(key)`, and `allUnits()`.

Edges never cross managers: a user unit's `Wants=foo.service` refers to `user:foo.service`. If a target key doesn't exist in the graph, create a placeholder `Unit` with `origin = Missing` so broken references are visible in the tree.

---

## 7. Loading data (`source/`)

### 7.1 `systemd_bus`
For each manager (system bus, then session bus):
1. Call `org.freedesktop.systemd1.Manager.ListUnits` to get the loaded units with their states and object paths.
2. For each loaded unit, call `org.freedesktop.DBus.Properties.GetAll` on the interface `org.freedesktop.systemd1.Unit`. Read `Id`, `Description`, `LoadState`, `ActiveState`, `SubState`, `UnitFileState`, `FragmentPath`, `SourcePath`, `DropInPaths`, `Requires`, `Requisite`, `Wants`, `BindsTo`, `PartOf`, `Upholds`, and `Triggers`.
3. For system `.service` units, also read `User` from `org.freedesktop.systemd1.Service`.
4. Call `Manager.ListUnitFiles` and add any unit file **not** already loaded as a `Unit` with `isLoaded = false`, its `unitFileState`, its `fragmentPath` set to the file path, and no edges.
   - **Do not call `LoadUnit`**, which would change manager state.
   - Skip template files (`foo@.service`) here; their instances already appear via `ListUnits`.

If the session bus is unavailable (e.g. a non-login SSH shell), load only the system manager and return a status message saying the user units are unavailable. Don't treat this as an error.

Plain sequential calls are fine. A few hundred `GetAll` calls finish well under a second.

### 7.2 `package_db`
On construction, iterate the alpm local DB once and build `std::map<std::string, std::string>` from absolute file path to package name. (alpm file names have no leading `/`, so add it.) Expose `std::string ownerOf(const std::string& path)`, which returns an empty string if the path is unowned. If alpm fails to initialise, log it and return an empty string for everything.

### 7.3 `classifier`
Pure logic with no I/O besides `std::filesystem::exists`. Apply these rules in order:

| Condition | Origin |
|---|---|
| `loadState == "not-found"` or placeholder | `Missing` |
| path under `/run/systemd/transient/` | `Transient` |
| path under `/run/systemd/generator*` | `Generated` |
| path under `~/.config/systemd/user/` | `UserCreated` |
| path under `/etc/systemd/` | `AdminCreated` |
| path under `/usr/lib/systemd/` or `/usr/lib/...` with owner `systemd` | `SystemdDefault` |
| path under `/usr/` with any other owner | `Package` (set `package`) |
| path under `/usr/` with no owner | `Unowned` (flag it: someone dropped a file there by hand) |
| anything else | `Unknown` |

Other fields:
- `locallyModified`: any drop-in path not under `/usr/lib/`.
- `shadowsPackagedUnit`: the unit is `AdminCreated` and the same filename exists under `/usr/lib/systemd/system/` (or the user equivalent).
- Masked units (`loadState == "masked"`) keep their origin. Masking is shown through `loadState`.
- `runAsUser`: for system services, `User=` or `"root"` if empty; for user units, the current user's name.

### 7.4 `loader`
This is the only entry point the rest of the app uses. `UnitGraph loadEverything(std::string& statusMessage)` runs the bus reads, classifies every unit, creates placeholders for missing targets, and builds the reverse edges.

### 7.5 `journal`
`std::vector<std::string> recentLogLines(const Unit&, int count)` runs `journalctl [--user] -u <name> -n <count> --no-pager -o short-iso` via `popen` and returns the lines. Quote the unit name safely: unit names are restricted, but still reject anything containing characters outside `[A-Za-z0-9:_.@\-\\]`.

Note for the README: a normal user can read system logs only if they're in the `wheel`, `adm`, or `systemd-journal` group.

---

## 8. Tree semantics (`ui/tree_view`)

### Two directions, toggled with `d`
- **Forward ("what does this pull in")**
  - Top-level roots are:
    - `default.target` of the system manager, labelled `System`.
    - `default.target` of the user manager, labelled `User (<name>)`.
    - A synthetic group `Not reachable from default.target`, containing loaded units that no forward walk from either root reaches.
    - A synthetic group `Not loaded`, containing the `isLoaded == false` units.
  - A node's children are its forward edges.
- **Reverse ("who needs this")**
  - The top level is a flat, sorted list of all units that pass the filters, with failed units first, then alphabetical.
  - A node's children are its reverse edges.

### Focus
`Enter` re-roots the current direction on the selected unit, so the tree shows only that unit and its subtree. `Backspace` returns to the previous root, using a simple stack of roots.

### Expansion
- Lazy: children are computed only when a node is expanded.
- Expansion state is a `std::set<std::string>` of **node paths** (`"system:default.target/system:multi-user.target/system:sshd.service"`), not unit keys, because the same unit appears in many places.

### Cycles
If a child is already an ancestor in the current path, show it with a `↻` marker and make it non-expandable. Repeats elsewhere in the tree are normal and expand as usual.

### Flattening
Each frame, walk the expanded tree into a `std::vector<Row>`. A `Row` holds the path, depth, unit key, edge kind from the parent, and whether the node is expanded, expandable, or a cycle. The cursor is an index into this vector. Keep the cursor on the same path after a refresh or filter change when possible.

### Row format
```
  ▾ ● sshd.service            wants   [pkg:openssh] ⚙
```
- Expand arrow: `▸` collapsed, `▾` expanded, blank for leaves.
- State icon with color:
  - `●` green = active
  - `○` dim = inactive
  - `✗` red = failed
  - `◐` yellow = transitioning
  - `?` = missing
  - `⊘` = masked
- Unit name, plus `(as <user>)` if a system service runs as a non-root user.
- Edge kind from the parent, dimmed, e.g. `wants`, `requires`, `triggers`.
- Origin tag: `[sys]`, `[pkg:<name>]`, `[admin]`, `[user]`, `[gen]`, `[transient]`, `[unowned]`, or `[missing]`.
- Markers: `⚙` for locally modified, `⇪` for shadowing a packaged unit.

---

## 9. Filters and search (`ui/filters`)

Filter state is a plain struct of `std::set`s and bools. It supports these groups, each value toggled independently:

- **Unit type:**
  - On by default: service, timer, socket, target, mount, automount, path, swap.
  - Off by default: device, scope, slice (they're mostly noise).
- **Active state:** active, inactive, failed, transitioning.
- **Manager:** system, user.
- **Origin:** every `Origin` value.
- **Flags:** "only locally modified", "only masked".

**Search:** a case-insensitive substring match against the name and description.

**Matching in tree mode:** a node is shown if it matches, or if any node below it in the *currently expanded* subtree matches. Ancestors that are only shown to give context are rendered dimmed.

**Problems preset (`p`):** toggles a filter that shows only failed units, missing units, masked units that another unit requires, and `Unowned` units. This is the "what could be wrong" view.

**Filter panel (`F`):** opens a panel of FTXUI `Checkbox` components grouped by the categories above. The panel just edits the filter struct, and the tree re-flattens on the next frame.

---

## 10. UI layout and keys (`ui/app`)

```
┌ / search ──────────────────────────────────────┐ [dir: forward] [filters: 3 off]
├ tree ──────────────────────────┬ details ──────┤
│ ...                            │ name, desc    │
│                                │ manager/user  │
│                                │ load/active   │
│                                │ file state    │
│                                │ origin/pkg    │
│                                │ paths/dropins │
│                                │ error (if any)│
├ journal (last 30 lines) ───────┴───────────────┤
│ ...                                            │
└ status: 412 units · 2 failed · user bus ok ────┘
```

| Key | Action |
|---|---|
| `↑/↓`, `j/k` | move cursor |
| `←/→`, `h/l` | collapse / expand |
| `Space` | toggle expand |
| `Enter` / `Backspace` | focus on unit / go back |
| `/` | focus search; `Esc` clears and leaves the search |
| `d` | toggle tree direction |
| `F` | filter panel |
| `p` | problems preset |
| `J` | show / hide the journal pane |
| `L` | open the full journal in a pager (`journalctl -u <name> -e`) |
| `s` / `S` | start / stop |
| `r` | restart |
| `R` | reload unit (`systemctl reload`) |
| `e` / `E` | enable / disable |
| `u` | reload all data from systemd |
| `?` | help overlay listing these keys |
| `q` | quit |

**Journal pane:** when visible, it reloads for the selected unit whenever the selection changes. Start synchronous. If moving the cursor feels slow, the user will ask for a debounced background load later; leave a one-line note in `journal_pane.cpp` saying so.

**Actions (`source/actions`):**
1. Show a small confirmation dialog: `Restart sshd.service (system)? y/n`.
2. Run `systemctl [--user] <verb> <name>` inside `screen.WithRestoredIO(...)`. That leaves the fullscreen UI so polkit can prompt for a password in the terminal.
3. Print the exit status and wait for Enter.
4. Reload that unit's data. Reloading everything with `u` is acceptable if that's simpler.

Actions are only offered for loaded units, except enable/disable, which also works on unit files.

---

## 11. Debuggability

- **`--dump` flag:** `nyst --dump` runs the loader, prints one tab-separated line per unit (`key, activeState, subState, unitFileState, origin, package, runAsUser, #deps, #dependents, flags, error`), prints the status message, then exits. This is the main tool for checking the data layer without the UI and must be kept working in every milestone.
- **Debug log:** if the environment variable `NYST_DEBUG=1` is set, append timestamped lines to `~/.cache/nyst/debug.log`. Log load timings, counts, bus failures, alpm failures, and every command executed. Otherwise logging does nothing. The logger is a free function `debugLog(const std::string&)` and nothing more.
- Per-unit read errors go into `Unit::error` and appear in the details pane, never silently swallowed.

---

## 12. Milestones

Stop after each one: build, fix all warnings, report.

1. **Skeleton + data layer.**
   - CMake project, `.clang-format`, `README.md`.
   - `model/`, all of `source/` except `journal` and `actions`, the debug logger, and `main.cpp` with `--dump`.
   - Build succeeds. The user will run `--dump` and compare against `systemctl`.
2. **Tree view.**
   - FTXUI app with the forward direction, expand/collapse, cursor, row format, cycle markers, and status bar.
   - Direction toggle and focus/back.
3. **Search and filters.** Search bar, filter struct, filter panel, problems preset, and ancestor-keeping matching.
4. **Details and journal.** Details pane, journal pane, `J`, and `L`.
5. **Actions.** Confirmation dialog, the `systemctl` commands with restored IO, refresh after an action, and the help overlay.
6. **(Only if the user asks)** Live updates: subscribe to `PropertiesChanged` on a background thread and push changes via `screen.Post()`.

---

## 13. Out of scope

Unless the user asks, don't add: tests, config files, themes, mouse support beyond FTXUI defaults, remote hosts, JSON output, packaging (PKGBUILD), or performance work.
