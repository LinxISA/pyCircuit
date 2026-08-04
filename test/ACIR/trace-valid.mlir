// RUN: %acir_opt_public %s | %FileCheck %s
// RUN: %acir_opt_public %s | %acir_opt_public | %FileCheck %s
// RUN: %acir_opt_public --pass-pipeline='builtin.module(canonicalize,cse)' %s | %FileCheck %s --check-prefix=EFFECTS

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Trace() parameters {} graph {
    ac.process @workload kind "workload" {
      %cursor0 = ac.trace.open source "pto"
      %cursor1, %raw, %advanced = ac.trace.next %cursor0 from source "pto" : i32
      %decoded = ac.trace.decode %raw : i32 to i64
      %position = ac.trace.position %cursor1 from source "pto"
      %eof = ac.trace.eof %cursor1 from source "pto"
      %observed = ac.probe @decoder kind "module" : i64
      %index_cursor0 = ac.trace.open source "index_records"
      %index_cursor1, %index_raw, %index_advanced = ac.trace.next %index_cursor0 from source "index_records" : index
      %index_decoded = ac.trace.decode %index_raw : index to i64
      ac.stat.add @decoded %decoded : i64
      ac.stat.add @position %position : index
      ac.stat.add @eof %eof : i1
      ac.stat.add @observed %observed : i64
      ac.stat.add @decoded %index_decoded : i64
      ac.yield_sim
    }
    ac.return
  }
}

// CHECK: ac.trace.open source "pto"
// CHECK: ac.trace.next
// CHECK: ac.trace.decode
// CHECK: ac.trace.position
// CHECK: ac.trace.eof
// EFFECTS: ac.trace.open
// EFFECTS: ac.trace.next
// EFFECTS: ac.trace.position
// EFFECTS: ac.trace.eof
// EFFECTS: ac.probe
// EFFECTS: ac.stat.add
// EFFECTS: ac.yield_sim
