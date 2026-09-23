# nyst — Not Your Systemd Tree

A terminal UI that shows every systemd unit on the machine (system and user managers) as a
navigable dependency tree, with running state, owner, origin (systemd / package / admin / user /
generated / transient / unowned / missing), and the kind of each dependency.

> Status: milestone 5 (tree, search, filters, details, journal, actions). Live updates are
> optional and not implemented.

## Build

Dependencies (Arch Linux):

```sh
sudo pacman -S --needed cmake gcc pkgconf sdbus-cpp pacman
```

FTXUI is not in the official repos, so CMake fetches release `v7.0.3` on the first configure
(needs `git` and network access once per build directory).

```sh
cmake -S . -B build
cmake --build build -j
./build/nyst          # interactive tree
./build/nyst --dump   # data layer only
```

## The tree

- **Forward** (default): `System` and `User (<name>)` are the two `default.target`s. Children are
  what a unit pulls in (`requires`, `wants`, ...). Units no walk from either root reaches are under
  *Not reachable from default.target*; unit files systemd has not loaded are under *Not loaded*.
- **Reverse** (`d`): a flat list of every unit, failed first. Children are who needs the unit
  (`required-by`, `wanted-by`, ...).
- `↻` marks a unit that already appears above it on the same branch (a dependency cycle).

Row format: `▾ ● name (as user)   edge-kind   [origin] ⚙ ⇪`

| Icon | Meaning | Marker | Meaning |
|---|---|---|---|
| `●` green | active | `⚙` | drop-in outside `/usr/lib` |
| `○` dim | inactive | `⇪` | `/etc` file shadows a packaged unit |
| `✗` red | failed | `↻` | cycle, not expandable |
| | | `⚠` | wanted by an active unit, never started |
| `◐` yellow | transitioning | | |
| `?` | missing | | |
| `⊘` | masked | | |

## Details and journal

- The **details** pane (right) shows everything known about the selected unit: description,
  manager and user, aliases, load/active/file state, origin and package, fragment/source/drop-in
  paths, edge counts, and any error hit while reading it.
- The **journal** pane (bottom) shows the unit's last 30 log lines and reloads whenever the
  selection changes. `J` hides/shows it (hidden = no journalctl calls). Mouse wheel scrolls it.
- `L` opens the full journal in journalctl's pager (`journalctl [--user] -u <name> -e`); quit the
  pager to come back.

## Actions

`s`/`S` start/stop, `r` restart, `R` reload, `e`/`E` enable/disable the selected unit.

1. A dialog asks first, e.g. `Restart sshd.service (system)?` (`y`, or click `[yes]`; `n`/`Esc`,
   or a click outside, cancels; `Enter` alone picks the focused button, which starts on `[no]`).
2. nyst leaves the fullscreen view and runs `systemctl [--user] <verb> <name>` in your terminal,
   so polkit can ask for a password there.
3. It prints the result and waits for `Enter`, then reloads everything. The status bar shows
   `restart sshd.service: done` or `...: failed (exit N)`.

Only loaded units can be started, stopped, restarted, or reloaded; enable/disable also work on
units that are only known as unit files. Missing units offer no actions. `?` shows every key.

## Search and filters

- `/` searches name and description (case-insensitive). `Enter` keeps the search, `Esc` clears it.
- `F` opens the filter panel: unit type, state, manager, origin, and "only locally modified" /
  "only masked". `device`, `scope`, and `slice` are off by default because they are mostly noise.
  Each group has a `[toggle all]` button, and `[toggle all groups]` flips every group at once
  (all on, or all off if everything is already on). The "only" flags are not affected.
- `p` toggles the **problems** view: failed units, missing units, units whose file could not be
  parsed, `unowned` units, masked units that another unit requires, and units that **never
  started this boot although an active unit wants them** (marked `⚠`; see below). It ignores the checkboxes
  so nothing broken can hide behind a type filter.

A row is shown if it passes the filters or if something in its *expanded* subtree does; rows kept
only for context are dimmed. In the forward tree the roots and groups always stay visible, and the
group counts show how many members pass (e.g. `Not loaded (3 of 275)`).

Searching in the forward tree **expands the way to every match**, using the shortest route from
`System`/`User` (or through the groups for units those don't reach), and puts the cursor on the
first match. A unit that appears in several places is revealed once; the reverse direction (`d`)
lists every match flat. You can still collapse revealed branches, and clearing the search puts
the tree back the way you had it.

### "Never started" (`⚠`)

A loaded unit is flagged when an active unit pulls it in (`Requires=`, `Wants=`, `BindsTo=`,
`Upholds=`) but it never left `inactive` this boot and no `Condition*=` check skipped it. That
usually means its start job was dropped, e.g. to break an ordering cycle, or that it was enabled
without being started. Oneshots that ran and exited, units you stopped, and units skipped by a
condition are not flagged. Devices are ignored as pullers: they only pull units in when plugged.

## Mouse

- **Tree:** click a row to select it; click its arrow, or click an already-selected row, to
  expand/collapse. The wheel moves the cursor.
- **Journal:** the wheel scrolls through the log lines.
- **Header:** click the search box to type, `[problems ...]` to toggle the problems view,
  `[dir: ...]` to flip the direction, and `[filters: N off]` to open the filter panel.
- **Filter panel:** click checkboxes and buttons; click outside the panel or `[close]` to close it.
- **Dialogs:** click `[yes]`/`[no]`; a click outside the confirmation cancels, any click closes help.

## `--dump`

`nyst --dump` loads everything, prints one tab-separated line per unit, then the status line,
and exits. Use it to check the data layer without the UI:

```
key  activeState  subState  unitFileState  origin  package  runAsUser  #deps  #dependents  flags  error
```

- `-` means the field is empty.
- `flags` is a comma list of `modified` (drop-in outside `/usr/lib`), `shadows` (an `/etc` file
  overrides a packaged unit of the same name), `masked`, `not-loaded` (only known as a unit file),
  and `never-started` (see the `⚠` marker).
- Lines starting with `#` are the header and the status message.

Handy comparisons:

```sh
./build/nyst --dump | awk -F'\t' '$2=="failed"'          # vs. systemctl --failed
./build/nyst --dump | awk -F'\t' '$5=="unowned"'         # files dropped in /usr by hand
./build/nyst --dump | awk -F'\t' '$5=="missing"'         # broken references
```

## Origin rules

Rules are applied in order to the unit's fragment path:

| Condition                                        | Origin      |
|--------------------------------------------------|-------------|
| `not-found` or referenced but unknown            | `missing`   |
| `/run/.../systemd/transient/`                    | `transient` |
| `/run/.../systemd/generator*`                    | `generated` |
| `~/.config/systemd/user/`, `~/.local/share/systemd/user/` | `user` |
| `/etc/systemd/`                                  | `admin`     |
| `/usr/...` owned by the `systemd` package        | `systemd`   |
| `/usr/...` owned by another package              | `package`   |
| `/usr/...` owned by no package                   | `unowned`   |
| anything else (e.g. devices, slices: no file)    | `unknown`   |

## Debug log

```sh
NYST_DEBUG=1 ./build/nyst --dump > /dev/null
cat ~/.cache/nyst/debug.log
```

Logs load timings, unit counts, bus and alpm failures, and every command executed (with its
exit code for actions and the full-journal pager).

## Journal access

A normal user can read system unit logs only if they belong to the `wheel`, `adm`, or
`systemd-journal` group.

## Keybindings

All keys:

| Key | Action |
|---|---|
| `↑/↓`, `j/k` | move cursor |
| `PgUp/PgDn`, `Home/End`, `g/G` | move by page / to start or end |
| `←/→`, `h/l` | collapse / expand (`←` on a collapsed row jumps to its parent) |
| `Space` | toggle expand |
| `Enter` / `Backspace` | focus on unit / go back |
| `d` | toggle tree direction |
| `/` | search (`Enter` keeps, `Esc` clears, `↑/↓` move while typing) |
| `F` | filter panel (arrows move, `Space`/`Enter` toggle, `Esc`/`F` close) |
| `p` | problems preset |
| `J` | show / hide the journal pane |
| `L` | full journal in a pager |
| `s` / `S` | start / stop (asks first) |
| `r` / `R` | restart / reload (asks first) |
| `e` / `E` | enable / disable (asks first) |
| `u` | reload all data from systemd |
| `?` | help overlay |
| `q` | quit |
