---
name: generated-code-postprocess-guard
description: Guards scripts that post-process generated code. Use when build scripts patch generated Verilog/SystemVerilog/C/C++/MLIR text, normalize ports/resets/names, rewrite filelists, or sync generated artifacts.
---

# Generated Code Postprocess Guard

## Instructions

Use this skill whenever generated output is modified by scripts after the generator runs.

Post-processing must be deterministic, repeatable, and fail-fast. Avoid fragile string edits that only work for today's generated text.

## Guard Checklist

1. Prefer structured parsing or narrowly scoped module-aware scanning over broad regex.
2. Never use "first occurrence" edits for syntax boundaries such as `);` unless the scope is proven.
3. Make transformations idempotent.
4. Fail with a clear error if the expected pattern is absent.
5. Clean stale generated directories before regeneration when names can change.
6. Clean stale synced deliverables before copying new artifacts.
7. Sync all required submodules and update filelists in the same step.
8. Verify post-processed output by searching generated artifacts, not only by trusting script success.

## Fragile Patterns To Avoid

- Inserting text after the first `);` in a Verilog file.
- Rewriting module names without checking endmodule comments and instantiation sites.
- Copying new generated files without deleting stale old files.
- Updating top RTL but forgetting filelist and submodule files.
- Patching generated RTL manually instead of making the build script reproducible.

## Required Validation

After changing a post-process script:

1. Force a clean regeneration.
2. Inspect generated output for the expected transformation.
3. Search for stale names/files.
4. Run the relevant build and simulation regression.
