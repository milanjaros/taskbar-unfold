# Taskbar Unfold

Standalone x64 EXE: separate taskbar buttons without labels, without Windhawk.

## Usage

Double-click `taskbar-unfold.exe`, or run:

```
taskbar-unfold.exe start
taskbar-unfold.exe stop
taskbar-unfold.exe probe
```

`start` activates label hooks and sets Windows to never combine buttons on all
taskbars. `stop` removes the hooks and restores the original registry values.
`probe` checks symbol configuration without changing Explorer. Run as the
signed-in user; administrator rights are not required.

Only **taskbar-unfold.exe** is needed. It contains the DLL and creates `.taskbar-unfold` beside
itself for the extracted DLL, configuration, downloaded symbols and settings
backup. Use a writable folder. Keep this folder and the EXE in place until you
have run `stop`.

On first use with a Windows build, matching PDB symbols are downloaded automatically from
Microsoft using the system DLL's PDB GUID and age. This requires internet access.
Missing or ambiguous symbols prevent activation. The first download may take time.

No autostart is installed. Run again after restarting Explorer or signing in.
Registry settings remain until `stop`. The DLL stays mapped after stopping until
Explorer exits so in-flight calls remain safe.

## Build and internals

Requirements: Windows 11 x64, MSVC with C++17 support, and the Windows SDK
(including `rc.exe`). No Windhawk SDK or third-party libraries are required.

From an **x64 Native Tools Command Prompt**, run these commands in the project
directory:

```bat
cl /nologo /std:c++17 /EHsc /W4 /MT /O2 /DBUILD_DLL /LD taskbar.cpp /Fe:payload.dll /link user32.lib
rc /nologo taskbar.rc
cl /nologo /std:c++17 /EHsc /W4 /MT /O2 taskbar.cpp taskbar.res /Fe:taskbar-unfold.exe /link dbghelp.lib user32.lib advapi32.lib urlmon.lib
```

These commands build `payload.dll`, embed it using `taskbar.rc`, and produce
`taskbar-unfold.exe`. A clean checkout needs no PDB
or binary symbol configuration to build. No build files or external DLL are
needed at runtime. The runtime caches verified addresses for subsequent starts.

For a custom build, `rc /d EMBED_SYMBOLS taskbar.rc` can additionally embed an
existing verified `symbols.bin`; normal builds do not embed machine-specific data.

Validated locally with MSVC 14.44.35207, Windows SDK 10.0.26100.0, and Windows
build 26300.9711. Activation and restoration were checked on that build; support
for other builds depends on their available symbols and compatible internals.

The DLL replaces only two HasLabel vtable entries; it does not patch shared
machine code, which could also affect the running indicator. Windows provides
uncombined buttons through TaskbarGlomLevel and MMTaskbarGlomLevel. A binary
backup preserves original values, including absent values. The DLL checks image
timestamp and size before using configured addresses.

Windows internals are undocumented and updates may require code changes as well
as new symbols. Other taskbar customizers can conflict. Existing buttons may
need a layout refresh or a new window before changes appear. Restarting Explorer
removes the in-memory hooks.

## License

GPL-3.0-only. HasLabel vtable technique adapted from taskbar-labels 1.5.1 by m417z:
https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-labels.wh.cpp

Reference source: `reference/taskbar-labels.wh.cpp`. Implementation: `taskbar.cpp`.
See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
