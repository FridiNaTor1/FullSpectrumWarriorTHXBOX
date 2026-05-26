# Recompiled Source Layout

The private generation step groups lifted functions by source path from the PDB
DBI module tables when available. MAP linker object ownership is still used to
assign function address ranges and as a fallback when the PDB has no source
file for a module.

Examples:

- `CActivateReport.obj` becomes `src/game/fsw/script/behaviors/scriptbehaviors/cactivatereport.c`
- `Animation:ZeroVideo.obj` becomes `src/game/libraries/source/zero/zerovideo.c`
- SDK/library modules with source records keep their PDB paths, such as `xapi/k32/launch.c`
- modules without source records fall back to MAP object paths under `src/game/external`, such as `external/BinkXbox.LTCG/rgb16mmx.c`

Each generated function keeps its stable recomp identifier, such as
`fn_00011080_Normalize`, and retains the original decorated symbol in the
function comment.

The current layout contains 1,458 generated source files and 46,875 emitted
functions. 1,179 source files are placed directly in the PDB-style `src/game`
tree; 279 source files without PDB source ownership are kept under
`src/game/external`. `src/game/recomp/source_manifest.json` records the
file/function counts and address ranges for each generated file.

`src/game/recomp` is intentionally limited to generated dispatch declarations,
manual recomp hooks, and the source manifest.
