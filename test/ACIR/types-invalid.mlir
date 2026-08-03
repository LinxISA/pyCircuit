// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/vector-zero.mlir 2>&1 | %FileCheck %s --check-prefix=VECTOR-ZERO
// RUN: %not %acir_opt %t/vector-negative.mlir 2>&1 | %FileCheck %s --check-prefix=VECTOR-NEGATIVE
// RUN: %not %acir_opt %t/channel-nested.mlir 2>&1 | %FileCheck %s --check-prefix=CHANNEL-NESTED
// RUN: %not %acir_opt %t/channel-standalone.mlir 2>&1 | %FileCheck %s --check-prefix=CHANNEL-STANDALONE
// RUN: %not %acir_opt %t/rate-numerator.mlir 2>&1 | %FileCheck %s --check-prefix=RATE-NUMERATOR
// RUN: %not %acir_opt %t/rate-denominator.mlir 2>&1 | %FileCheck %s --check-prefix=RATE-DENOMINATOR
// RUN: %not %acir_opt %t/duration-data-unit.mlir 2>&1 | %FileCheck %s --check-prefix=DURATION-DATA
// RUN: %not %acir_opt %t/invalid-role.mlir 2>&1 | %FileCheck %s --check-prefix=INVALID-ROLE

// VECTOR-ZERO: error: vector length must be positive
// VECTOR-NEGATIVE: error: vector length must be positive
// CHANNEL-NESTED: error: channel types cannot be nested inside value types
// CHANNEL-STANDALONE: error: channel type is only permitted in an ac.interface channel declaration
// RATE-NUMERATOR: error: rate numerator must be a data unit
// RATE-DENOMINATOR: error: rate denominator must be a time unit
// DURATION-DATA: error: duration requires a time unit
// INVALID-ROLE: error: failed to parse ACIR_EndpointType parameter 'role'

//--- vector-zero.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.vector<0 x i8>
}

//--- vector-negative.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.vector<-2 x i8>
}

//--- channel-nested.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.optional<!ac.channel<i8, @ready_valid>>
}

//--- channel-standalone.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.channel<i8, @ready_valid>
}

//--- rate-numerator.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<cycles, cycles>
}

//--- rate-denominator.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bytes, packets>
}

//--- duration-data-unit.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<bytes>
}

//--- invalid-role.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.endpoint<@MemoryPort, "target">
}
