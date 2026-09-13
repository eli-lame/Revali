# Hardware

KiCad projects for the vehicle's electronics. Design rationale lives in
[docs/hardware_roadmap.md](../docs/hardware_roadmap.md); the pin map is
authoritative in [docs/hardware.md](../docs/hardware.md).

The boards live here rather than in a separate repository because the board and
the firmware have to version together. When a GPIO moves, the schematic and
`firmware/include/board/pins.h` change in the same commit — split across two
repositories there is no way to tell, later, which board a given firmware
commit was built for.

## Layout

```
hardware/
  lib/                            symbols and footprints shared by every board
    HopCopter.pretty/               our own footprints
    Genli.kicad_sym                 our own symbols
    ESP32_Footprints.pretty/        third party, MIT
    esp32_30pin_revised.kicad_sym   third party, MIT
    ESP32-DevKit-V1-DOIT.LICENSE    its licence — keep it with the files
  v1-prototype/                   first board — archived, never fabricated
  stage2-carrier/                 current work — see its README for the spec
```

## Libraries are vendored, not installed

Every custom or third-party symbol and footprint lives in `hardware/lib/` and
is registered **per project**, never globally. A globally installed library is
invisible to the repository: the project opens on the machine that installed it
and nowhere else. v1 had exactly this problem — its symbols were referenced by
an absolute path into `OneDrive/Documents/`, so a clone of its repo could not
resolve them.

Register in *Preferences → Manage Symbol / Footprint Libraries → Project
Specific Libraries*, always with `${KIPRJMOD}`-relative paths:

| Nickname | Path |
|---|---|
| `HopCopter` | `${KIPRJMOD}/../lib/HopCopter.pretty` |
| `ESP32_Footprints` | `${KIPRJMOD}/../lib/ESP32_Footprints.pretty` |
| `Genli` | `${KIPRJMOD}/../lib/Genli.kicad_sym` |
| `esp32_30pin_revised` | `${KIPRJMOD}/../lib/esp32_30pin_revised.kicad_sym` |

**The nicknames are load-bearing — do not rename them.** Symbol and footprint
assignments are stored as `Nickname:Name`, so a nickname is a hard reference
across every sheet and every board that uses it. `HopCopter` and `Genli` are
leftovers from earlier names for this project; renaming them would break every
existing assignment for no benefit. Moving the *files* is safe; renaming the
nickname is not.

If a nickname is also registered in your global table, remove the global entry
— duplicate nicknames across the two tables produce conflicts.

The ESP32 devkit symbol and footprint are third-party
([ESP32-DevKit-V1-DOIT](https://github.com/asyaugi/ESP32-DevKit-V1-DOIT), MIT).
Its licence file sits beside them and stays there.

### Known gap

v1's PCB also references `PCM_SparkFun-Connector:ScrewTerminal_1x02_P5.0mm`,
which comes from a library installed through KiCad's Plugin and Content
Manager and is **not** vendored here. v1 is archived and that screw terminal is
deleted in Stage 2, so this is recorded rather than fixed. Do not introduce new
PCM dependencies in Stage 2 — vendor anything that isn't a KiCad stock library.

## Board naming and archives

Each board gets its own directory. Superseded boards stay, unmodified, as a
record — [hardware_roadmap.md](../docs/hardware_roadmap.md) documents v1's
defects, and that section means nothing without the schematic it refers to.

`v1-prototype/` is a snapshot. Its commit history lives in its original
repository, [eli-lame/Genli](https://github.com/eli-lame/Genli), which stays
frozen; the copy here exists so the board is visible beside the firmware it was
designed for. It keeps its original `Genli` file names — the directory name
carries the meaning, and renaming a KiCad project is the riskiest operation
available for no gain on a board that will never be edited again.

A new board is a **new KiCad project**, not a copy of the previous one. Copying
inherits the previous netlist, and v1's netlist contains the reversed
`ESC1`–`ESC4` mapping and a power section built around a part that is no longer
used. Footprints are worth reusing; net topology is not.

## Moving a KiCad project without breaking it

The rule is one change at a time, verified by opening the project between each.

1. **Copy**, do not move, the project into place. Keep the original until the
   copy is confirmed working.
2. Open it in KiCad. Confirm the schematic and the board both load, and that
   footprints resolve — the PCB editor lists unresolved ones loudly.
3. Commit that, unchanged.
4. *Then* move the `.pretty` folder to `hardware/lib/` and repoint the library
   path as above. Reopen, confirm footprints still resolve, commit.
5. Only then delete the original.

A KiCad project is `<name>.kicad_pro`, `<name>.kicad_sch` and
`<name>.kicad_pcb`, and **the root schematic must share the project's base
name**. So renaming a project means renaming all three consistently — use
*File → Save As*, which does it correctly, rather than renaming files by hand.
There is no need to rename an archived board; the directory name carries the
meaning.

## What is not committed

The root `.gitignore` excludes KiCad's per-user and regenerable files:
`*-backups/`, `*.kicad_prl`, `fp-info-cache`, `*.net`, autosave files, and
`.history/` from the VS Code local-history extension. Everything else in a
project directory is source and belongs in the repository.

Fabrication outputs (gerbers, drill files, BOM, pick-and-place) go in a
`production/` subdirectory of the board they belong to, and **are** committed —
they are the exact bytes sent to the fab, which is worth being able to
reproduce.
