// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.protocol"() <{sym_name = "wire"}> ({
    "ac.role"() <{sym_name = "sender", dual = @receiver, cardinality = "exclusive"}> : () -> ()
    "ac.role"() <{sym_name = "receiver", dual = @sender, cardinality = "exclusive"}> : () -> ()
    "ac.state"() <{sym_name = "idle", initial = true, terminal = false}> : () -> ()
    "ac.event"() <{sym_name = "send", from = @sender, to = @receiver, payload = i8, action = "offer"}> : () -> ()
    "ac.transition"() <{source = @idle, target = @idle, event = @send, transfer = true}> ({}) : () -> ()
    "ac.guarantee"() <{kind = "backpressure", value = "none"}> : () -> ()
    "ac.guarantee"() <{kind = "ordering", value = "fifo"}> : () -> ()
    "ac.guarantee"() <{kind = "delivery", value = "exactly_once"}> : () -> ()
    "ac.guarantee"() <{kind = "completion", value = "on_accept"}> : () -> ()
    "ac.guarantee"() <{kind = "max_inflight", value = 1 : i64}> : () -> ()
  }) : () -> ()
  "ac.interface"() <{sym_name = "Wire"}> ({
    "ac.role"() <{sym_name = "source", dual = @sink, cardinality = "exclusive"}> : () -> ()
    "ac.role"() <{sym_name = "sink", dual = @source, cardinality = "exclusive"}> : () -> ()
    "ac.port"() <{sym_name = "data", type = !ac.channel<i8, @wire>, from = @source, to = @sink}> : () -> ()
  }) : () -> ()
  "ac.interface"() <{sym_name = "SharedWire"}> ({
    "ac.role"() <{sym_name = "source", dual = @sink, cardinality = "shared"}> : () -> ()
    "ac.role"() <{sym_name = "sink", dual = @source, cardinality = "shared"}> : () -> ()
  }) : () -> ()
  %endpoint = "builtin.unrealized_conversion_cast"() : () -> !ac.endpoint<@Wire, @source>
  %shared = "builtin.unrealized_conversion_cast"() : () -> !ac.endpoint<@SharedWire, @source>
  %use0 = "builtin.unrealized_conversion_cast"(%shared) : (!ac.endpoint<@SharedWire, @source>) -> i1
  %use1 = "builtin.unrealized_conversion_cast"(%shared) : (!ac.endpoint<@SharedWire, @source>) -> i1
}

// CHECK: "ac.interface"
// CHECK: "ac.port"
// CHECK-SAME: !ac.channel<i8, @wire>
// CHECK: !ac.endpoint<@Wire, @source>
