// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/forked-cursor.mlir 2>&1 | %FileCheck %s --check-prefix=CURSOR
// RUN: %not %acir_opt %t/noncursor.mlir 2>&1 | %FileCheck %s --check-prefix=TYPE
// RUN: %not %acir_opt %t/wrong-owner.mlir 2>&1 | %FileCheck %s --check-prefix=OWNER
// RUN: %not %acir_opt %t/duplicate-document-owner.mlir 2>&1 | %FileCheck %s --check-prefix=MULTI-OWNER
// RUN: %not %acir_opt %t/duplicate-cursor.mlir 2>&1 | %FileCheck %s --check-prefix=DUPLICATE-CURSOR
// RUN: %not %acir_opt %t/source-is-not-path.mlir 2>&1 | %FileCheck %s --check-prefix=SOURCE-ID

//--- forked-cursor.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "workload" {
      %cursor = ac.trace.open source "input"
      %next, %raw, %advanced = ac.trace.next %cursor from source "input" : i32
      %other, %raw2, %advanced2 = ac.trace.next %cursor from source "input" : i32
      ac.yield_sim
    }
    ac.return
  }
}
// CURSOR: trace cursor must have at most one consuming use

//--- noncursor.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "workload" {
      %bad = arith.constant 0 : index
      %next, %raw, %advanced = ac.trace.next %bad from source "input" : i32
      ac.yield_sim
    }
    ac.return
  }
}
// TYPE: trace cursor must originate from ac.trace.open or ac.trace.next

//--- wrong-owner.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "workload" {
      %cursor = ac.trace.open source "input"
      %next, %raw, %advanced = ac.trace.next %cursor from source "other" : i32
      ac.yield_sim
    }
    ac.return
  }
}
// OWNER: trace cursor owner does not match 'from source'

//--- duplicate-document-owner.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @left kind "workload" {
      %cursor = ac.trace.open source "pto"
      ac.yield_sim
    }
    ac.process @right kind "workload" {
      %cursor = ac.trace.open source "pto"
      ac.yield_sim
    }
    ac.return
  }
}
// MULTI-OWNER: trace source 'pto' must have exactly one cursor owner

//--- source-is-not-path.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @workload kind "workload" {
      %cursor = ac.trace.open source "traces/model.json"
      ac.yield_sim
    }
    ac.return
  }
}
// SOURCE-ID: trace source must be one stable logical identifier segment

//--- duplicate-cursor.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @workload kind "workload" {
      %first = ac.trace.open source "pto"
      %second = ac.trace.open source "pto"
      ac.yield_sim
    }
    ac.return
  }
}
// DUPLICATE-CURSOR: trace source 'pto' must have exactly one cursor owner
