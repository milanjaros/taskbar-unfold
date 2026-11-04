# Third-party attribution

Taskbar Unfold adapts the HasLabel vtable-slot technique from **Taskbar Labels
for Windows 11**, version 1.5.1, by **m417z** (Michael Maltsev).

- Upstream source: https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-labels.wh.cpp
- Author: https://github.com/m417z
- License: GNU General Public License version 3.0, GPL-3.0-only.
- Local reference copy: `reference/taskbar-labels.wh.cpp`.

The reference retains its upstream metadata and license notice. Taskbar Unfold
replaces the Windhawk runtime with a standalone executable, an embedded DLL,
Microsoft PDB symbol resolution, and native taskbar settings backup/restore.

Microsoft PDB files are downloaded at runtime when required. They are not part
of the source repository. See `LICENSE` for the project's GPLv3 license text.
