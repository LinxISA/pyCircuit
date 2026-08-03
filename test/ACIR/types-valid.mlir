// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s
// RUN: %acir_opt --emit-bytecode -o %t.bc %s
// RUN: %acir_opt %t.bc | %FileCheck %s

// This file covers all 16 SSA-legal ACIR v0.1 public types. Channel's 17th
// parser/printer case is covered by ACIRTypesTest.PublicTypeInventoryRoundTrips.
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "builtin.unrealized_conversion_cast"() : () -> !ac.struct<@Header>
  "builtin.unrealized_conversion_cast"() : () -> !ac.packet<@Request>
  "builtin.unrealized_conversion_cast"() : () -> !ac.transaction<@Dma>
  "builtin.unrealized_conversion_cast"() : () -> !ac.enum<@Opcode>
  "builtin.unrealized_conversion_cast"() : () -> !ac.union<@Payload>
  "builtin.unrealized_conversion_cast"() : () -> !ac.optional<i32>
  "builtin.unrealized_conversion_cast"() : () -> !ac.list<!ac.struct<@Entry>>
  "builtin.unrealized_conversion_cast"() : () -> !ac.vector<4 x i8>
  "builtin.unrealized_conversion_cast"() : () -> !ac.vector<9223372036854775807 x i8>
  "builtin.unrealized_conversion_cast"() : () -> !ac.flow<!ac.packet<@Request>, @ready_valid>
  "builtin.unrealized_conversion_cast"() : () -> !ac.endpoint<@MemoryPort, @target>
  "builtin.unrealized_conversion_cast"() : () -> !ac.resource_ref<@Memory, @reader>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<cycles>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<ticks>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<seconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<milliseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<microseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<nanoseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.duration<picoseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bytes, cycles>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bits, ticks>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<entries, seconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<packets, milliseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<transactions, microseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bytes, nanoseconds>
  "builtin.unrealized_conversion_cast"() : () -> !ac.rate<bytes, picoseconds>
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
// CHECK: !ac.vector<9223372036854775807 x i8>
// CHECK: !ac.flow<!ac.packet<@Request>, @ready_valid>
// CHECK: !ac.endpoint<@MemoryPort, @target>
// CHECK: !ac.resource_ref<@Memory, @reader>
// CHECK: !ac.duration<cycles>
// CHECK: !ac.duration<ticks>
// CHECK: !ac.duration<seconds>
// CHECK: !ac.duration<milliseconds>
// CHECK: !ac.duration<microseconds>
// CHECK: !ac.duration<nanoseconds>
// CHECK: !ac.duration<picoseconds>
// CHECK: !ac.rate<bytes, cycles>
// CHECK: !ac.rate<bits, ticks>
// CHECK: !ac.rate<entries, seconds>
// CHECK: !ac.rate<packets, milliseconds>
// CHECK: !ac.rate<transactions, microseconds>
// CHECK: !ac.rate<bytes, nanoseconds>
// CHECK: !ac.rate<bytes, picoseconds>
// CHECK: !ac.event<!ac.transaction<@Dma>>
// CHECK: !ac.address<@global>
// CHECK: !ac.resource_token<@Memory>
