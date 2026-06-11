# OvenbirdBT

`OvenbirdBT` is a TUI torrent client built on libtorrent and FTXUI.

<img width="1541" height="851" alt="Screenshot" src="https://github.com/user-attachments/assets/a84bfcf4-7eb4-4fbf-9071-2888a9a4a442" />

## Features

- Download and upload multiple torrents simultaneously.
- Support for `.torrent` files and magnet links.
- Partial download.
- Restore torrents across restarts.
- Terminal User Interface with mouse support.
- Select torrents in the list and pause/resume, remove, or inspect them.
- Show progress, state, peers, seeds, download/upload rates.
- Cross-platform C++ code.

## Dependencies

- [libtorrent](https://github.com/arvidn/libtorrent)
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- [simpleini](https://github.com/brofield/simpleini)

Linux packages vary by distribution. On Debian/Ubuntu systems, install CMake, Ninja, a C++17 compiler, libtorrent-rasterbar development files, and FTXUI development files. SimpleIni is used as a local header-only file at `src/SimpleIni.h`.

On Windows, install vcpkg, Visual Studio 2022 or Build Tools for Visual Studio with the "Desktop development with C++" workload.

## Build

Linux:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/ovenbirdbt
```

Windows with vcpkg and MSVC from a regular PowerShell:

```powershell
cmake --preset windows-vs-vcpkg
cmake --build --preset windows-vs-vcpkg
.\build\windows-vs\Release\ovenbirdbt.exe
```

## Configuration

`ovenbirdbt` reads an INI configuration file at startup. By default it looks for `%APPDATA%\ovenbirdbt\config.ini` on Windows, `~/.config/ovenbirdbt/config.ini` on Linux, or `$XDG_CONFIG_HOME/ovenbirdbt/config.ini`.  
`--config PATH` can point to a different file.

Example:

```ini
[paths]
save = /path/to/downloads
state = /path/to/ovenbirdbt-state

[libtorrent]
user_agent = OvenbirdBT
enable_dht = true
enable_lsd = true
enable_upnp = true
enable_natpmp = true
tracker_completion_timeout = 60
tracker_receive_timeout = 10
stop_tracker_timeout = 5
peer_connect_timeout = 15
request_timeout = 20
listen_interfaces = 0.0.0.0:6881,[::]:6881
# Comma-separated. When omitted, ovenbirdbt adds its built-in default trackers.
# Set to an empty value to disable built-in additional trackers.
additional_trackers = http://tracker.example/announce, udp://tracker.example:1337/announce
# Rate limits are in Kb/s; 0 means unlimited.
upload_rate_limit = 0
download_rate_limit = 0
```

Command-line `--save` and `--state` values override the INI file for that launch.

Print the application version:

```sh
ovenbirdbt --version
```

By default, ovenbirdbt stores resume data under `%APPDATA%\ovenbirdbt` on Windows, `~/.local/state/ovenbirdbt` on Linux, or `$XDG_STATE_HOME/ovenbirdbt`

You can also pass startup torrents:

```sh
ovenbirdbt --save ~/Downloads "magnet:?xt=urn:btih:..." ./example.torrent
```

## Controls

In main list use the arrow keys to select a torrent, `Enter` or `i` to open its information window, `t` to open partial download file selection, `a` to open the Add torrent window, `p` to pause or resume, `d` to delete, and `q` for exit. In the information window, use `1`-`4` to switch tabs. Confirm dialogs with `Enter`, or cancel with `Esc`. In the partial download screen, use `Space` or `Enter` to toggle the selected file, `a` to select all files, and `n` to select none. The same actions are also exposed as clickable on-screen buttons.
