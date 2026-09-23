# nyst — Not Your Systemd Tree

A terminal UI that shows every systemd unit on the machine (system and user managers) as a
navigable dependency tree, with running state, owner, origin (systemd / package / admin / user /
generated / transient / unowned / missing), and the kind of each dependency.

> Status: milestone 1 (data layer + `--dump`). The TUI arrives in milestone 2.

## Build

Dependencies (Arch Linux):

```sh
sudo pacman -S --needed cmake gcc pkgconf sdbus-cpp pacman
```

FTXUI is not in the official repos. It will be fetched and pinned by CMake once the UI lands.

```sh
cmake -S . -B build
cmake --build build -j
./build/nyst --dump
```

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

## Keybindings (planned)

| Key | Action |
|---|---|
| `↑/↓`, `j/k` | move cursor |
| `←/→`, `h/l` | collapse / expand |
| `Space` | toggle expand |
| `Enter` / `Backspace` | focus on unit / go back |
| `/` | search (`Esc` clears) |
| `d` | toggle tree direction |
| `F` | filter panel |
| `p` | problems preset |
| `J` / `L` | journal pane / full journal in pager |
| `s` / `S` | start / stop |
| `r` / `R` | restart / reload |
| `e` / `E` | enable / disable |
| `u` | reload all data |
| `?` | help |
| `q` | quit |
