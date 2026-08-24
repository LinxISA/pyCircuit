// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/queue-of-var.mlir 2>&1 | %FileCheck %s --check-prefix=QUEUE-OF-VAR
// RUN: %not %acir_opt %t/var-of-queue.mlir 2>&1 | %FileCheck %s --check-prefix=VAR-OF-QUEUE
// RUN: %not %acir_opt %t/queue-of-function.mlir 2>&1 | %FileCheck %s --check-prefix=QUEUE-OF-FUNCTION
// RUN: %not %acir_opt %t/var-of-function.mlir 2>&1 | %FileCheck %s --check-prefix=VAR-OF-FUNCTION
// RUN: %not %acir_opt %t/queue-of-list.mlir 2>&1 | %FileCheck %s --check-prefix=QUEUE-OF-LIST
// RUN: %not %acir_opt %t/var-of-list.mlir 2>&1 | %FileCheck %s --check-prefix=VAR-OF-LIST

// QUEUE-OF-VAR: error: queue payload must be an immutable ACIR value type
// VAR-OF-QUEUE: error: var payload must be an immutable ACIR value type
// QUEUE-OF-FUNCTION: error: queue payload must be an immutable ACIR value type
// VAR-OF-FUNCTION: error: var payload must be an immutable ACIR value type
// QUEUE-OF-LIST: error: queue payload must be an immutable ACIR value type
// VAR-OF-LIST: error: var payload must be an immutable ACIR value type

//--- queue-of-var.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.queue<!ac.var<i32>>
}

//--- var-of-queue.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.var<!ac.queue<i32>>
}

//--- queue-of-function.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.queue<(i32) -> i32>
}

//--- var-of-function.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.var<(i32) -> i32>
}

//--- queue-of-list.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.queue<!ac.list<i32>>
}

//--- var-of-list.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.var<!ac.list<i32>>
}
