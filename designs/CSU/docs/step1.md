# Step 1 — Read programming style documents and examples (CSU)

**Method reference:** `docs/pycircuit_implementation_method.md` § Step 1  
**Output summary:** Recorded below; authoritative CSU choices also appear in `csu_implementation_requirements.md` Part II.

**CSU converted specs (for context):** Block-specific **Markdown** exports of XLSX / DOCX / PDF live under **`designs/CSU/docs/converted/`** (see `converted/README.md`, regenerate via `python3 designs/CSU/scripts/export_specs_to_md.py`). Step 1 does not require reading them in depth, but know they exist so later steps cite searchable text instead of only binary files.

**Feature inventory:** SRC-07 is mirrored in **`feature_list.md`** (**F-001–F-075**) with a **digest index** plus **§ SRC-07 digest heading checklist (full)** — every `#` / `##` / `###` in **`converted/SRC-07_linxcore950_csu_design_spec.md`** maps to an **F-xxx** or **—** (meta/TOC). Skim before coding.

---

## 1. Goal

Internalize how **pyCircuit** expresses hardware so CSU implementation does not fight the toolchain:

- Static elaboration and **JIT subset** (when using `compile_cycle_aware` without `eager=True`).
- **V5** logical cycles (`CycleAwareDomain.next()`) vs **ClockHandle** occurrence model (`clk.next()` + `m.assign`) — use **one primary** style per top-level to avoid confusion.
- Testbench **tick/transfer** semantics (`docs/TESTBENCH.md`).

---

## 1b. Cycle / timing contract（本步骤）

本步 **不** 产生 RTL 周期表；须建立概念：**V5 occurrence**（`domain.next()`）与 **同一 `clk` 上的寄存器拍数** 不同。CSU 的 **可验证周期预算** 见 **`cycle_budget.md`**；Step 5 起必须写明 **`domain.next()` 次数** 与 MLIR **`pyc.reg`** 契约。

---

## 2. Document reading list (ordered)

| # | Path | Depth | Focus for CSU |
|---|------|-------|---------------|
| 1 | `docs/PyCircuit_V5_Spec.md` | Full read | `cas`, `mux`, `domain.signal()`, `domain.call()`, `submodule_input`, `wire_of`, `compile_cycle_aware` |
| 3 | `docs/FRONTEND_API.md` | Read | When a child `@module` is needed inside CSU |
| 4 | `docs/TESTBENCH.md` | Read | How `tb_csu.py` will drive/expect |
| 5 | `docs/tutorial/unified-signal-model.md` | Read | Relationship V5 vs `@module` |
| 6 | `AGENTS.md` | Read | Gate-first; no backend-only semantics |
| 7 | `docs/QUICKSTART.md` | Skim | Build / PYTHONPATH |

---

## 3. Example designs to open in editor

| Example | Path | What to notice |
|---------|------|----------------|
| V5 large state | `designs/RegisterFile/regfile.py` | `domain.state`, `domain.next()`, massive `mux` |
| V5 + TB | `designs/BypassUnit/bypass_unit.py`, `tb_bypass_unit.py` | Structure for `tb_csu.py` |
| Complex ctrl | `designs/IssueQueue/issq.py` | Arbitration patterns (do not copy scale) |

**Exercise (recommended):** Run `compile_cycle_aware` on BypassUnit-style design with `eager=True` and inspect `emit_mlir()` output once.

---

## 4. CSU decisions (Step 1 result)

| Decision | Choice |
|----------|--------|
| Primary authoring | **PyCircuit V5** top-level (`CycleAwareCircuit`) |
| Bring-up compile | `compile_cycle_aware(..., eager=True)` until control is stable |
| Hierarchy | Introduce `@module` children only when spec clearly separates units |
| Style reference | **RegisterFile** + **BypassUnit** |

---

## 5. Completion checklist

- [ ] Read items in §2 at stated depth  
- [ ] Opened all examples in §3  
- [ ] Confirmed understanding of `push`/`pop` for nested helpers  
- [ ] Recorded any questions in `designs/CSU/ASSUMPTIONS.md`

**Optional:** If any later step feels too large, use **`workflow_substeps.md`** (2a–2e, 3a–3f, …) to split work into reviewable chunks.

**Next step:** `step2.md`
