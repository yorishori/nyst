# nyst — Not Your Systemd Tree

A terminal UI that shows every systemd unit on the machine (system and user managers) as a
navigable dependency tree, with running state, owner, origin (systemd / package / admin / user /
generated / transient / unowned / missing), and the kind of each dependency.

> Status: milestone 2 (tree view). Search/filters, details/journal, and actions are next.

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
| `◐` yellow | transitioning | | |
| `?` | missing | | |
| `⊘` | masked | | |

## `--dump`

`nyst --dump` loads everything, prints one tab-separated line per unit, then the status line,
and exits. Use it to check the data layer without the UI:

```
key  activeState  subState  unitFileState  origin  package  runAsUser  #deps  #dependents  flags  error
```

- `-` means the field is empty.
- `flags` is a comma list of `modified` (drop-in outside `/usr/lib`), `shadows` (an `/etc` file
  overrides a packaged unit of the same name), `masked`, and `not-loaded` (only known as a unit file).
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

Logs load timings, unit counts, bus and alpm failures, and (later) every command executed.

## Journal access

A normal user can read system unit logs only if they belong to the `wheel`, `adm`, or
`systemd-journal` group.

## Keybindings

Working now:

| Key | Action |
|---|---|
| `↑/↓`, `j/k` | move cursor |
| `PgUp/PgDn`, `Home/End`, `g/G` | move by page / to start or end |
| `←/→`, `h/l` | collapse / expand (`←` on a collapsed row jumps to its parent) |
| `Space` | toggle expand |
| `Enter` / `Backspace` | focus on unit / go back |
| `d` | toggle tree direction |
| `u` | reload all data from systemd |
| `q` | quit |

Planned:

| Key | Action |
|---|---|
| `/` | search (`Esc` clears) |
| `F` | filter panel |
| `p` | problems preset |
| `J` / `L` | journal pane / full journal in pager |
| `s` / `S` | start / stop |
| `r` / `R` | restart / reload |
| `e` / `E` | enable / disable |
| `?` | help |
