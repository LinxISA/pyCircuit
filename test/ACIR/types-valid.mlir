// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s
// RUN: %acir_opt --emit-bytecode -o %t.bc %s
// RUN: %acir_opt %t.bc | %FileCheck %s
// RUN: python3 %S/check-public-type-inventory.py %S/types-valid.mlir %S/types-invalid.mlir

// Every ACIR v0.1 public type is intentionally spelled out here. This is also
// the source consumed by the public-type inventory check.
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.struct<@Header>
  "builtin.unrealized_conversion_cast"() : () -> !ac.packet<@Request>
  "builtin.unrealized_conversion_cast"() : () -> !ac.transaction<@Dma>
  "builtin.unrealized_conversion_cast"() : () -> !ac.enum<@Opcode>
  "builtin.unrealized_conversion_cast"() : () -> !ac.union<@Payload>
  "builtin.unrealized_conversion_cast"() : () -> !ac.optional<i32>
  "builtin.unrealized_conversion_cast"() : () -> !ac.list<!ac.struct<@Entry>>
  "builtin.unrealized_conversion_cast"() : () -> !ac.vector<4 x i8>
  "builtin.unrealized_conversion_cast"() : () -> !ac.flow<!ac.packet<@Request>, @ready_valid>
  "builtin.unrealized_conversion_cast"() : () -> !ac.endpoint<@MemoryPort, @target>
  "builtin.unrealized_conversion_cast"() : () -> !ac.resource_ref<@Memory, @reader>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<cycles>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bytes, cycles>
  "builtin.unrealized_conversion_cast"() : () -> !ac.event<!ac.transaction<@Dma>>
  "builtin.unrealized_conversion_cast"() : () -> !ac.address<@global>
  "builtin.unrealized_conversion_cast"() : () -> !ac.resource_token<@Memory>
}

// CHECK: !ac.struct<@Header>
// CHECK: !ac.packet<@Request>
// CHECK: !ac.transaction<@Dma>
// CHECK: !ac.enum<@Opcode>
// CHECK: !ac.union<@Payload>
// CHECK: !ac.optional<i32>
// CHECK: !ac.list<!ac.struct<@Entry>>
// CHECK: !ac.vector<4 x i8>
// CHECK: !ac.flow<!ac.packet<@Request>, @ready_valid>
// CHECK: !ac.endpoint<@MemoryPort, @target>
// CHECK: !ac.resource_ref<@Memory, @reader>
// CHECK: !ac.duration<cycles>
// CHECK: !ac.rate<bytes, cycles>
// CHECK: !ac.event<!ac.transaction<@Dma>>
// CHECK: !ac.address<@global>
// CHECK: !ac.resource_token<@Memory>
