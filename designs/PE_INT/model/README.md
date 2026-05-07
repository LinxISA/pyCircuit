# model（模型與模型測試）

本目錄集中管理所有「驗證比對所依據的模型」與模型測試。

## 內容

- `ref_model.py`: 四模式數學語義與打包/解包參考模型
- `pe_int_pycircuit_eval.py`: cycle-accurate pipeline 模型（L=4）
- `test_pe_int.py`: 模型層回歸測試（不直接跑 RTL simulator）
- `gen_rtl_case_vectors.py`: 依模型自動產生 `tb_rtl/case` 的 testcase 向量與 expected
- `model.f`: model 檔案清單（以 `$PE_INT` 絕對路徑為開頭）

## 原則

- 凡是驗證需要 golden/model 比對，應以本目錄內容為依據。
- `tb/` 與 `tb_rtl/` 的用例若有 expected 值，來源需可追溯到 `model/`。

## 執行

```bash
python model/test_pe_int.py
```
