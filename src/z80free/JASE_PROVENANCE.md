## Z80free in JASE

This directory is an unmodified copy of Z80free, a standalone Z80 core by
Sergio Costas Rodriguez (Raster Software Vigo), (C) 2008-2009, GPL-3 (see
`COPYING`). It's the real station-CPU core Fuse's FDD3000 patch uses
(`peripherals/disk/z80free/` in `~/src/Fuse_FDD3000/build/fuse-1.10.1/`),
not Fuse's own main Z80 core.

Used here as `Fdd3000`'s station CPU, replacing JASE's own `z80cpp` for
that one role (the host CPU still uses `z80cpp` -- see
`include/z80cpp/`). This is the ONE place in JASE that carries GPL-3
code instead of the project's usual MIT tree: the firewall policy that
keeps every other peripheral facts-only (clean-room, no copied code) is
explicitly lifted for FDD3000 only -- see `CLAUDE.md` and project memory
`project_jase_fdd3000.md`/`project_jase_overview.md`. Any file in JASE
that links against this code is bound by GPL-3 for as long as it does.

Files are copied as-is from the Fuse source tree; do not hand-edit the
generated `Z80free_codesXXXX.c` tables (see `README.txt`'s own
"HOW Z80FREE IS IMPLEMENTED" section for how they're regenerated from
Z80FREE_GENCODEXXXX.TXT via a Python generator, not maintained upstream
in this repo).
