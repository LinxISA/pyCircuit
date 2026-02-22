# pyCircuit

## Scope
`tools/pyCircuit` is the pyCircuit workspace mirror used for LinxISA model generation, compiler/frontend flows, and differential validation support.

## Upstream
- Repository: `https://github.com/LinxISA/pyCircuit`
- Merge-back target branch: `main`

## What This Submodule Owns
- pyCircuit frontend/model pipeline (`pyc` flows)
- Build/sim scripts used by Linx bring-up
- Model-side tooling for trace and integration checks

## Canonical Build and Test Commands
Run from `/Users/zhoubot/linx-isa/tools/pyCircuit`.

```bash
bash flows/scripts/pyc build
bash flows/scripts/run_examples.sh
bash flows/scripts/run_sims.sh
```

Optional superproject differential suite:

```bash
python3 /Users/zhoubot/linx-isa/tools/bringup/run_model_diff_suite.py \
  --root /Users/zhoubot/linx-isa \
  --suite /Users/zhoubot/linx-isa/avs/model/linx_model_diff_suite.yaml \
  --profile release-strict
```

## LinxISA Integration Touchpoints
- Model parity support for bring-up gates
- Shared trace/debug workflows with `rtl/LinxCore`
- Invoked by selected superproject bring-up scripts

## Related Docs
- `/Users/zhoubot/linx-isa/docs/project/navigation.md`
- `/Users/zhoubot/linx-isa/docs/bringup/`
- `/Users/zhoubot/linx-isa/avs/model/`
