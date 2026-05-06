# PE_INT

定點／整數向量 MAC 單元。實作流程固定為：

`spec.md` -> `python/pe_int_pycircuit.py` -> `pycircuit.cli build` -> `rtl/` + `tb/`

## Baseline

- 規格來源：`docs/spec.md`
- 當前 baseline：`v2.0.5`（以 `docs/spec.md` 檔首標註為準）

## 目錄

- `docs/`: 正式規格
- `python/`: PyCircuit 設計與建構腳本
- `model/`: golden 模型與模型回歸測試
- `tb/`: pyCircuit 測試（非 RTL 模擬）
- `tb_rtl/`: 專用 RTL 測試環境（Verilog testbench + cases）
- `sim/`: 互動式一鍵仿真入口（可選 iverilog / verilator + 波形）
- `rtl/`: 僅放「由 build 產生」之 RTL 產物說明

## 主要檔案

- `model/ref_model.py`: 四模式數學與打包/解包參考模型
- `model/pe_int_pycircuit_eval.py`: cycle-accurate golden pipeline model（L=4）
- `model/test_pe_int.py`: 模型層隨機回歸（功能與時序對齊）
- `model/gen_rtl_case_vectors.py`: 自動重生 `tb_rtl/case` 的 expected 向量
- `python/pe_int_pycircuit.py`: pyCircuit frontend 設計入口（source of RTL）
- `python/build.py`: 統一 build 指令（呼叫 `python -m pycircuit.cli build`）
- `tb/tb_pe_int_pycircuit.py`: pyCircuit 原生 testbench（給 `pycc`/sim flow）
- `tb_rtl/case/*.v`: RTL 專用測試案例（單模式 sanity + mode switch random）
- `sim/run_all_wsl.sh`: 一鍵跑 `iverilog` + `verilator` RTL 回歸
- `sim/run_sim.sh`: 互動式一鍵仿真（每題 5 秒 timeout，含 seed 輸入）
- `filelist/pe_int.f`: RTL filelist（以 `$PE_INT` 絕對路徑為開頭）
- `tb_rtl/tb.f`: 驗證環境/用例 filelist（以 `$PE_INT` 絕對路徑為開頭）
- `model/model.f`: model filelist（以 `$PE_INT` 絕對路徑為開頭）

## 快速使用

1) 先跑 golden model 測試：

```bash
python model/test_pe_int.py
```

2) 產生 RTL / sim 產物（需先具備 pyCircuit 環境）：

```bash
python python/build.py --target both --out-dir build/pe_int
```

3) 執行 RTL 專用回歸（WSL）：

```bash
bash sim/run_all_wsl.sh
```

或使用互動式一鍵仿真入口（WSL）：

```bash
bash sim/run_sim.sh
```

## 流程約束

- 不手寫 `rtl/*.v`。
- `vld_out` 與 `out0`/`out1` 對齊，`2a` 下 `out1` 採 hold 策略以避免無謂切換。
- 任何新機器若沒有既有 profile，先依 `LinxISA/pyCircuit` repo 完成環境再執行本流程。
