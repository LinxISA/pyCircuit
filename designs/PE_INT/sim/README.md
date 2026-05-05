# sim（一鍵仿真入口）

本目錄提供互動式一鍵腳本：

- `run_sim.sh`

## 功能

執行前會詢問（每題等待 5 秒，逾時套用預設值）：

1. testcase 向量 seed（預設：當下時間戳）
2. 使用哪個 simulator：`iverilog` / `verilator`（預設 `iverilog`）
3. 是否產生波形（預設 `No`）
4. 波形格式：`vcd` / `fst`（預設 `vcd`）

腳本會先透過 `model/model.f` 解析並執行：

```bash
python3 <resolved from model/model.f> --seed <seed>
```

再以 filelist（`filelist/pe_int.f` + `tb_rtl/tb.f`）跑 `tb_rtl/case` 下全部 testcase。

## 可重現性

- 同一個 seed 會生成完全一致的 `tc_mode_switch_random` 向量與 expected。
- 同一個 seed 也會生成完全一致的 sanity vectors（`tc_mode2a/2b/2c/2d_sanity`）；不同 seed 會改變其向量。
- `iverilog` 與 `verilator` 都使用同一份 seed 生成流程，因此兩者都保證上述 seed 行為一致。
- 每個 testcase 都有獨立 log，且開頭會記錄該次 seed，便於下次還原。
- log 位置：
  - `sim/logs/<simulator>/<timestamp>_run<idx>/seed_<seed>_<case>.log`

## 執行

```bash
bash sim/run_sim.sh
```

## 波形輸出

- 啟用波形後，輸出位置為：
  - `sim/waves/<simulator>/<case>/wave.<fmt>`

## 參數差異（腳本已內建處理）

- `iverilog`：以 `iverilog + vvp` 路徑執行；波形以 `$dumpfile/$dumpvars` 生成（實務預設 VCD）。
- `verilator`：依格式自動加：
  - `--trace`（VCD）
  - `--trace-fst`（FST）
