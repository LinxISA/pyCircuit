# Step 9 — Incremental implementation and regression

**Method reference:** `docs/pycircuit_implementation_method.md` § Step 9  
**Deliverable files:** `incremental_plan.md`, `designs/CSU/IMPLEMENTATION_LOG.md` (create on first increment)

**Converted specs:** If an increment depends on a **changed** vendor **.xlsx / .docx / .pdf**, run **`python3 designs/CSU/scripts/export_specs_to_md.py`** before merging, then refresh `port_list.md` / `feature_list.md` / `csu.LEGAL_REQ_OPCODE_VALUES` as needed and note the regeneration in `IMPLEMENTATION_LOG.md`.

**Feature backlog:** Prefer tagging increments with **`feature_list.md` IDs** (e.g. Inc-x implements **F-042 BRQ FSM** + tests). After SRC-07 edits, re-run the Step 2 §3 workflow item **8** (feature parity vs digest).

**Large Step 9?** **`workflow_substeps.md`** § Step 9 (**9a–9c**): split **Inc-x** in `incremental_plan.md`, always run full verification after each micro-merge, refresh docs.

---

## 1. Goal

Grow `csu.py` from **shell** to **full CSU** with **continuous regression** — no big-bang merge.

---

## 1b. Cycle / timing contract（本步骤）

每个 **Inc-x** 合并后：**更新 `cycle_budget.md`**（occurrence 段数、`domain.next()` 次数、显式 `state`/`cycle` 个数、MLIR **`pyc.reg` / `_v5_bal_` 黄金值**），并同步 **`csu.py` 中 `INCx_*` 常量** 与 `assert_*_mlir_cycle_contract`。`IMPLEMENTATION_LOG.md` 记录变更原因。

---

## 2. Process (each increment)

1. Pick next **Inc-x** from `incremental_plan.md`.  
2. Implement minimal code; avoid unrelated refactor.  
3. Add or extend tests per **Inc** column.  
4. Run **full** existing suite.  
5. Append **IMPLEMENTATION_LOG.md** entry:

```text
## YYYY-MM-DD Inc-x
- Features: F-00y, ...
- Files: csu.py, tb_csu.py
- Tests: T-00a, ...
- Result: PASS
- Command: ...
```

---

## 3. Git / review practice

- Prefer **one increment per commit** (or per PR if squashed with clear message).  
- PR description: `Inc-x: <short title>`, lists features + tests.

---

## 4. When to switch `eager=False`

Enable JIT (`compile_cycle_aware` default) only when:

- No `if Wire` misuse remains, and  
- `docs/PyCircuit_V5_Spec.md` patterns satisfied, or  
- Child uses `@module` with clear boundaries.

---

## 5. Compiler / IR changes

If increment needs new dialect rules:

1. Stop feature work.  
2. Follow `AGENTS.md`: verifier/pass first.  
3. Document decision IDs in `IMPLEMENTATION_LOG.md`.

---

## 6. Completion checklist

- [ ] All increments in `incremental_plan.md` marked **Done**  
- [ ] `IMPLEMENTATION_LOG.md` complete  
- [ ] CI (if any) green on last increment

**Next step:** `step10.md`
