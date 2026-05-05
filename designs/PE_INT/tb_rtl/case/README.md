# RTL 測試案例（case）

本目錄每個 `.v` 檔是獨立 testcase（可單獨編譯/仿真）。

## 案例清單與目的

- `tc_mode2a_sanity.v`
  - 驗證 mode 2a（S8xS8）數學結果正確。
  - 驗證 2a 下 `out1` 不應被更新（保持前一個雙路模式值）。

- `tc_mode2b_sanity.v`
  - 驗證 mode 2b（S8xS4）雙路輸出 `out0/out1` 正確。

- `tc_mode2c_sanity.v`
  - 驗證 mode 2c（S5xS5 + E1）雙路輸出正確。

- `tc_mode2d_sanity.v`
  - 驗證 mode 2d（S8xS5）雙路輸出正確。

- `tc_mode_switch_random.v`
  - 驗證 back-to-back 切 mode 時，`vld -> vld_out` 對齊與順序正確。
  - 驗證 mixed traffic（含 `vld=0` 空檔）下，輸出值與預期一致。
  - 預期值來源需可追溯到 `model/` 下的參考模型。

## 命名規範

- `tc_*`：可直接做 simulator top module 使用。
- 每個 case 內建 PASS/FAIL 訊息與 `$fatal`，方便 CI 直接判斷。

## expected 自動生成

- 產生工具：`model/model.f` 內的 `gen_rtl_case_vectors.py`
- 產生輸出：`tb_rtl/case/generated/*.vh`
- testcase 透過 `` `include `` 使用這些自動檔案，不手抄 expected。

手動重生（透過 filelist 解析）：

```bash
export PE_INT="$(pwd)"
sed "s|\$PE_INT|${PE_INT}|g" model/model.f > build/.model.resolved.f
python "$(awk '/gen_rtl_case_vectors\.py$/ {print; exit}' build/.model.resolved.f)"
```
