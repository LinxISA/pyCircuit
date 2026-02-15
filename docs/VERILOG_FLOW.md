# Open-source Verilog Flow (Refactored)

pyCircuit emits plain Verilog modules and uses small SystemVerilog testbenches in `designs/examples/`.

Supported open-source tooling:

- Icarus Verilog (`iverilog` + `vvp`) for quick simulation
- Verilator for lint and faster compiled simulation
- GTKWave for waveform viewing

Generated artifacts are out-of-tree and go under `.pycircuit_out/`.

## 1) Install tools

## macOS (Homebrew)

```bash
brew install icarus-verilog verilator gtkwave
```

## Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y iverilog verilator gtkwave
```

## 2) Validate toolchain and generate RTL

Check tools:

```bash
python3 flows/tools/pyc_flow.py doctor
```

Regenerate example outputs:

```bash
python3 flows/tools/pyc_flow.py regen --examples
```

Regenerate LinxCore outputs:

```bash
python3 flows/tools/pyc_flow.py regen --linxcore
```

Default `regen` with no flags runs both.

FastFwd FE override example:

```bash
python3 flows/tools/pyc_flow.py regen --examples --fastfwd-nfe 8
```

## 3) Run Verilog simulations

## FastFwd

```bash
python3 flows/tools/pyc_flow.py verilog-sim fastfwd_pyc +max_cycles=500 +max_pkts=1000 +seed=1
```

## Issue queue

```bash
python3 flows/tools/pyc_flow.py verilog-sim issue_queue_2picker
```

## Linx CPU (recommended tool: Verilator)

```bash
python3 flows/tools/pyc_flow.py verilog-sim linx_cpu_pyc --tool verilator \
  +memh=designs/examples/linx_cpu/programs/test_or.memh +expected=0000ff00
```

## 4) Lint generated Verilog

```bash
python3 flows/tools/pyc_flow.py verilog-lint fastfwd_pyc
python3 flows/tools/pyc_flow.py verilog-lint issue_queue_2picker
python3 flows/tools/pyc_flow.py verilog-lint linx_cpu_pyc
```

## 5) Open waveforms

```bash
python3 flows/tools/pyc_flow.py wave .pycircuit_out/examples/fastfwd_pyc/tb_fastfwd_pyc_sv.vcd
```

## 6) Artifact locations

Common output roots:

- FastFwd: `.pycircuit_out/examples/fastfwd_pyc/`
- Issue queue TB traces: `.pycircuit_out/examples/tb_issue_queue_2picker/`
- Linx CPU traces: `.pycircuit_out/examples/linx_cpu_pyc/`

Typical files:

- `*.vcd` waveform dumps
- `*.log` textual logs
- `verilator/` build directories

## 7) FastFwd C++ vs Verilog cross-check

```bash
python3 flows/tools/pyc_flow.py fastfwd-crosscheck --tool iverilog --seed 1 --cycles 200 --packets 400
```

Cross-check outputs are written under:

- `.pycircuit_out/examples/fastfwd_pyc/crosscheck/`

## 8) Manual simulator invocation notes

Generated RTL includes primitive includes like:

```verilog
`include "pyc_reg.v"
```

Add runtime primitive include paths when invoking simulators manually:

- Icarus: `-I runtime/verilog`
- Verilator: `-Iruntime/verilog`

`flows/tools/pyc_flow.py` adds these include paths automatically.
