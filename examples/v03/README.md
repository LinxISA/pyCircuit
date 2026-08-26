# Parameterized high-level block examples

`davincioo_jit.py` is the first v0.3 vertical slice. It keeps every dynamic
connection as a typed Queue SSA edge, freezes one `CoreConfig` through
`ac.jit`, and uses only the simple high-level block names `compute`, `route`,
`pipeline`, `merge`, `schedule`, and `reorder`.

Only `compute` carries a lambda. The remaining blocks select typed payload
fields with compile-time descriptors and lower to the existing optimized
gfsim and PYC/Verilog providers.

```bash
PYTHONPATH=src python - <<'PY'
from examples.v03.davincioo_jit import specialization

open("build/davincioo-v03.ac.mlir", "w").write(specialization.lower_acir())
open("build/davincioo-v03.cpp", "w").write(specialization.lower_cpp())
PY

c++ -std=c++20 -I include -fsyntax-only build/davincioo-v03.cpp
```

`specialization.materialize_cpp(cache_root)` compiles the generated C++ on
first use and returns the cached object on later calls. The corresponding
`materialize_pyc(...)` API consumes explicit pinned `acir-queue-pycgen`,
`pycc`, toolchain metadata, C++ compiler, and Verilator paths and publishes one
content-addressed PYC/C++/Verilog bundle.

`multirate_compute.py` freezes `rate=4` into each logical SimQueue and into the
`Compute<..., 4, ...>` / `Pipeline<..., 2, 4>` C++ template identities. The
QueueGraph and gfsim paths implement the proposal limit now. PYC intentionally
rejects `rate>1` with `requires explicit lane lowering`; this prevents treating
one shared FIFO as four unrelated queues while the ordered multi-enqueue FIFO
lowering is still under implementation.
