# Generated RTL

`rtl/` 只存放由 `pyCircuit` / `pycc` 建構流程產生的輸出。

## 產生方式

在 repo 根目錄執行：

```bash
python python/build.py --target rtl --out-dir build/pe_int
```

產物位置依 `pycircuit.cli build` 版本可能有差異，通常在 `build/pe_int/` 下。

## 原則

- 不手寫 `rtl/*.v`。
- RTL 內容以 `python/pe_int_pycircuit.py` 與 `tb/tb_pe_int_pycircuit.py` 為唯一來源。
- RTL 功能仿真請使用 `tb_rtl/`（不要把 RTL testcase 放回 `tb/`）。
