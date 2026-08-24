// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s
// RUN: %acir_opt --emit-bytecode -o %t.bc %s
// RUN: %acir_opt %t.bc | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 4 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %response = ac.memory %input entries 16 init 0 result_field "data" depth 4 latency 1 address {
  ^address(%item: !ac.var<!ac.struct<@types::@Request>>):
    %address = ac.var.get %item field "address" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i8>
    ac.memory.yield %address : !ac.var<i8>
  } write {
  ^write(%item: !ac.var<!ac.struct<@types::@Request>>):
    %write = ac.var.get %item field "write" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i1>
    ac.memory.yield %write : !ac.var<i1>
  } data {
  ^data(%item: !ac.var<!ac.struct<@types::@Request>>):
    %data = ac.var.get %item field "data" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i16>
    ac.memory.yield %data : !ac.var<i16>
  } : !ac.queue<!ac.struct<@types::@Request>> -> !ac.queue<!ac.struct<@types::@Request>>
  ac.sink %response : !ac.queue<!ac.struct<@types::@Request>>
}

// CHECK: ac.memory
// CHECK: entries 16 init 0 result_field "data" depth 4 latency 1
// CHECK-COUNT-3: ac.memory.yield
