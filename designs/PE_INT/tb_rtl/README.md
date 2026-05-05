# RTL 測試環境（tb_rtl）

本目錄專門放 **RTL 仿真** 所需檔案，與 `tb/` 下的 pyCircuit testbench 分離。

## 目錄說明

- `case/`: 各個 RTL testcase（單模式 sanity + 模式切換 random）
- `case/generated/`: 由 `model/gen_rtl_case_vectors.py` 自動產生的 testcase 向量與 expected
- `tb.f`: 驗證環境/用例 filelist（以 `$PE_INT` 絕對路徑為開頭）
- `sim/run_all_wsl.sh`: 在 WSL 內批次跑 `iverilog` / `verilator`

## 快速執行（WSL）

在 repo root（`/mnt/d/src_code/PE_INT`）執行：

```bash
bash sim/run_all_wsl.sh
```

此腳本會先透過 `model/model.f` 解析並執行：

```bash
python3 <resolved from model/model.f>
```

再透過 filelist（`filelist/pe_int.f` + `tb_rtl/tb.f`）進行 simulator 回歸。

## 單一用例執行範例

### iverilog

```bash
export PE_INT="$(pwd)"
sed "s|\$PE_INT|${PE_INT}|g" filelist/pe_int.f > build/.pe_int.resolved.f
sed "s|\$PE_INT|${PE_INT}|g" tb_rtl/tb.f > build/.tb.resolved.f
iverilog -g2012 -s tc_mode2b_sanity -f build/.pe_int.resolved.f -f build/.tb.resolved.f -o build/tc_mode2b_sanity.out
vvp build/tc_mode2b_sanity.out
```

### verilator

```bash
export PE_INT="$(pwd)"
sed "s|\$PE_INT|${PE_INT}|g" filelist/pe_int.f > build/.pe_int.resolved.f
sed "s|\$PE_INT|${PE_INT}|g" tb_rtl/tb.f > build/.tb.resolved.f
verilator --binary --timing -Wall -Wno-fatal \
  -f build/.pe_int.resolved.f -f build/.tb.resolved.f \
  --top-module tc_mode2b_sanity -o tc_mode2b_sanity
./obj_dir/tc_mode2b_sanity
```
