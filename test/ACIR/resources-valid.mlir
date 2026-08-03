// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.protocol"() <{sym_name = "fifo"}> ({
    "ac.role"() <{sym_name = "sender", dual = @receiver, cardinality = "exclusive"}> : () -> ()
    "ac.role"() <{sym_name = "receiver", dual = @sender, cardinality = "exclusive"}> : () -> ()
    "ac.state"() <{sym_name = "idle", initial = true, terminal = false}> : () -> ()
    "ac.state"() <{sym_name = "pending", initial = false, terminal = false}> : () -> ()
    "ac.state"() <{sym_name = "done", initial = false, terminal = true}> : () -> ()
    "ac.event"() <{sym_name = "push", from = @sender, to = @receiver, payload = i32, action = "offer"}> : () -> ()
    "ac.event"() <{sym_name = "accept", from = @receiver, to = @sender, payload = i32, action = "accept"}> : () -> ()
    "ac.event"() <{sym_name = "cancel", from = @sender, to = @receiver, payload = i32, action = "cancel"}> : () -> ()
    "ac.transition"() <{source = @idle, target = @pending, event = @push, retain = true}> ({}) : () -> ()
    "ac.transition"() <{source = @pending, target = @done, event = @accept, transfer = true}> ({}) : () -> ()
    "ac.transition"() <{source = @pending, target = @done, event = @cancel}> ({}) : () -> ()
    "ac.guarantee"() <{kind = "ordering", value = "fifo"}> : () -> ()
    "ac.guarantee"() <{kind = "delivery", value = "exactly_once"}> : () -> ()
    "ac.guarantee"() <{kind = "completion", value = "on_accept"}> : () -> ()
    "ac.guarantee"() <{kind = "backpressure", value = "capacity"}> : () -> ()
    "ac.guarantee"() <{kind = "stable_pending", value = true}> : () -> ()
    "ac.guarantee"() <{kind = "max_inflight", value = 1 : i64}> : () -> ()
  }) : () -> ()
  ac.module @Top() parameters {} graph {
    ac.time_domain @core period 2 phase 0 scale 1
    ac.queue @ready payload i32 entries 8 bytes 64 ordering "fifo" protocol @fifo
        ownership "exclusive" id "ready" path "ready" watermarks {low = 2 : i64, high = 6 : i64}
    ac.event_queue @done payload !ac.event<i32> capacity 16 ordering "time_then_sequence"
        domain @core id "done" path "done"
    ac.instance @scheduler of @Arb() static {} id "scheduler" path "scheduler" : () -> ()
    ac.resource @compute capacity 4 issue_width 2 ii 1
        latency {kind = "fixed", ticks = 3 : i64}
        lifecycle {reservation = "propose_commit", release = "balanced", cancellation = "explicit"}
        ownership "shared" arbiter @scheduler classes []
        id "compute" path "compute"
    ac.return
  }
  ac.module @Arb() parameters {} graph { ac.return }
}

// CHECK: ac.queue @ready
// CHECK: ac.event_queue @done
// CHECK: ac.resource @compute
