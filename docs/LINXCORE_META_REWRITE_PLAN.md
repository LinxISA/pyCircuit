# LinxCore Meta Rewrite Plan (Post v3.4)

This document maps new v3.4 frontend APIs to the next LinxCore migration targets.

## Scope

- No LinxCore source mutation in v3.4.
- Use this as the execution map for the immediate follow-on milestone.

## Priority hotspots

1. `/Users/zhoubot/LinxCore/src/top/top.py`
   - Problem: high manual output fanout and explicit repetitive bindings.
   - v3.4 mapping:
     - `StructSpec` for top-level IO groups
     - `ConnectorStruct` + `inputs/outputs`
     - `array` for repeated stage instantiations and probes

2. `/Users/zhoubot/LinxCore/src/bcc/backend/engine.py`
   - Problem: repeated indexed wiring and per-slot argument assembly.
   - v3.4 mapping:
     - `StructSpec` transforms for lane/slot payloads
     - `module_family(...).vector(...)` for repeated stage modules
     - `array` for named clusters (ALU/BRU/LSU/CMD)

3. `/Users/zhoubot/LinxCore/src/bcc/backend/rob.py`
   - Problem: large manual per-slot port declarations.
   - v3.4 mapping:
     - `StructSpec` generation for dispatch/issue/commit slices
     - `array` for ROB entry update elements
     - strict `meta.bind(...)` to prevent key drift

4. `/Users/zhoubot/LinxCore/src/common/interfaces.py`
   - Problem: rich interface schema not fully leveraged for autowiring.
   - v3.4 mapping:
     - template conversion from interface tables -> `StructSpec` catalogs
     - reusable templates for `select_fields`/`drop_fields`/`rename_field` derivations

## Recommended migration order

1. Top-level IO and boundary wiring (`top.py`).
2. Backend structural arrays (`engine.py`).
3. ROB and issue/decode repetitive slices (`rob.py` + related files).
4. Full config-space DSE wrappers on top of module collections.

## Acceptance goals for follow-on

- Deterministic instance naming across repeated compiles.
- Zero manual repeated port loops in migrated regions.
- Strict key/width/signed checks at all module boundaries.
- Existing LinxCore smoke/perf flows remain green.
