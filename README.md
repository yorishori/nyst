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

## Loading

nyst opens immediately and loads in the background (about 2 s); the tree says "loading
units..." until the data arrives. Reloads (`u`, and the one after every action) also run in the
background: you keep browsing the old data, with "loading units..." in the status bar, until the
new data replaces it. Quitting during a load waits for it to finish.

## Live updates

While nyst runs it listens to systemd's change signals (both managers). Units it already knows
update in place within about a third of a second: state icons, the details pane, the problems
view, and the unit counts all follow along, and the journal pane refetches if the selected unit
changed. A finished `daemon-reload`, or a unit that starts running but was not loaded before,
triggers a full background reload instead. Units that are merely loaded for a moment (e.g. by
`systemctl status`) are ignored.

## Status bar

The left side shows the unit counts, anything unusual about loading (a bus that could not be
reached, jobs dropped at boot), and the outcome of the last action. The right side shows only
the few keys that make sense for the selected row: e.g. `r restart · L full log · c unit file`
for a failed service, `e enable · m mask · c unit file` for a unit file that is not loaded.
`?` lists every key.

## Tabs

The header has five tabs (`1`–`5`, `Tab`/`Shift+Tab`, or click). Search and filters apply to all
of them, and a unit focused with `Enter` stays focused when you switch.

1. **Tree**: `System` and `User (<name>)` are the two `default.target`s; children are what a unit
   pulls in (`requires`, `wants`, ...). Units neither root reaches are under *Not reachable from
   default.target*; unit files systemd has not loaded are under *Not loaded*.
2. **Dependents**: a flat list of every unit, failed first; children are who needs the unit
   (`required-by`, `wanted-by`, ...). `d` flips between Tree and Dependents.
3. **Boot**: units in the order they started, counted from kernel start. Rows show
   `+start (duration)`. Units started after their manager finished booting (restarts, socket
   activation, your login) come next, marked `after boot`, then units that never started.
   systemd only keeps the *last* start time, so a restarted unit moves to the "after boot" part.
4. **Slowest**: by startup time, slowest first, like `systemd-analyze blame`.
5. **Problems**: only units that look broken (see below). Its tab shows how many there are
   (`⚠N`); `p` jumps there and back.

The flat lists (2–5) all expand to show who needs a unit. `↻` marks a unit that already appears
above it on the same branch (a dependency cycle). The tree window's title shows the tab and any
focused unit.

Row format: `▾ ● name (as user)   edge-kind   [origin] ⚙ ⇪`

| Icon | Meaning | Marker | Meaning |
|---|---|---|---|
| `●` green | active | `⚙` | drop-in outside `/usr/lib` |
| `○` dim | inactive | `⇪` | `/etc` file shadows a packaged unit |
| `✗` red | failed | `↻` | cycle, not expandable |
| | | `⚠` | never started though wanted, or dropped by a boot ordering cycle |
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
- `c` shows the unit file plus all its drop-ins (`systemctl [--user] cat <name>`) the same way.

## Actions

`s`/`S` start/stop, `r` restart, `R` reload, `e`/`E` enable/disable, `m`/`M` mask/unmask the
selected unit, and `D` runs `daemon-reload` for the selected unit's manager (system if none).

1. A dialog asks first, e.g. `Restart sshd.service (system)?` (`y`, or click `[yes]`; `n`/`Esc`,
   or a click outside, cancels; `Enter` alone picks the focused button, which starts on `[no]`).
2. nyst leaves the fullscreen view and runs `systemctl [--user] <verb> <name>` in your terminal,
   so polkit can ask for a password there.
3. It prints the result and waits for `Enter`, then reloads everything. The status bar shows
   `restart sshd.service: done` or `...: failed (exit N)`.

Only loaded units can be started, stopped, restarted, or reloaded; enable/disable/mask/unmask
also work on units that are only known as unit files. Mask is offered only for unmasked units
and unmask only for masked ones. Missing units offer no actions. `?` shows every key.

## Search and filters

- `/` searches name and description (case-insensitive). `Enter` keeps the search, `Esc` clears it.
- `F` opens the filter panel: unit type, state, manager, origin, and "only locally modified" /
  "only masked". `device`, `scope`, and `slice` are off by default because they are mostly noise.
  Each group has a `[toggle all]` button, and `[toggle all groups]` flips every group at once
  (all on, or all off if everything is already on). The "only" flags are not affected.
- The **Problems** tab (`5` or `p`) lists failed units, missing units, units whose file could not be
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

### Boot ordering cycles (`⚠`)

When systemd finds an ordering cycle at boot, it drops one start job to break it, and that unit
silently never starts. nyst reads those messages from the current boot's journal (both managers),
marks each dropped unit with `⚠`, counts them in the status bar, and shows the cycle in the
details pane as `a → b → … → a` (each unit waits for the next). Fixing any one link breaks the
cycle; a unit you wrote yourself (e.g. in `/etc/systemd/system/`) is the usual suspect. Takes
effect on the next boot.

## Mouse

- **Tree:** click a row to select it; click its arrow, or click an already-selected row, to
  expand/collapse. The wheel moves the cursor.
- **Details and journal:** the wheel scrolls them.
- **Header:** click a tab to switch to it, the search box to type, and `[filters: N off]` to open
  the filter panel.
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
  `never-started`, and `dropped-by-cycle` (see the `⚠` markers).
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
| `1`–`5`, `Tab` / `Shift+Tab` | switch tab: Tree, Dependents, Boot, Slowest, Problems |
| `d` | Tree ↔ Dependents |
| `p` | Problems ↔ previous tab |
| `/` | search (`Enter` keeps, `Esc` clears, `↑/↓` move while typing) |
| `F` | filter panel (arrows move, `Space`/`Enter` toggle, `Esc`/`F` close) |
| `J` | show / hide the journal pane |
| `L` | full journal in a pager |
| `c` | unit file and drop-ins in a pager |
| `s` / `S` | start / stop (asks first) |
| `r` / `R` | restart / reload (asks first) |
| `e` / `E` | enable / disable (asks first) |
| `m` / `M` | mask / unmask (asks first) |
| `D` | daemon-reload (asks first) |
| `u` | reload all data from systemd |
| `?` | help overlay |
| `q` | quit |
