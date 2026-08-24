// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/entries.mlir 2>&1 | %FileCheck %s --check-prefix=ENTRIES
// RUN: %not %acir_opt %t/init.mlir 2>&1 | %FileCheck %s --check-prefix=INIT
// RUN: %not %acir_opt %t/field.mlir 2>&1 | %FileCheck %s --check-prefix=FIELD
// RUN: %not %acir_opt %t/write.mlir 2>&1 | %FileCheck %s --check-prefix=WRITE
// RUN: %not %acir_opt %t/data.mlir 2>&1 | %FileCheck %s --check-prefix=DATA

// ENTRIES: error: 'ac.memory' op entries, depth, and latency must be positive
// INIT: error: 'ac.memory' op v0.2 memory init must be zero
// FIELD: error: 'ac.memory' op unknown result_field 'missing'
// WRITE: error: 'ac.memory' op write must yield !ac.var<i1>
// DATA: error: 'ac.memory' op data must match result_field type

//--- entries.mlir
builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 1 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %bad = ac.memory %input entries 0 init 0 result_field "data" depth 1 latency 1 address {
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
}

//--- init.mlir
builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 1 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %bad = ac.memory %input entries 16 init 1 result_field "data" depth 1 latency 1 address {
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
}

//--- field.mlir
builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 1 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %bad = ac.memory %input entries 16 init 0 result_field "missing" depth 1 latency 1 address {
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
}

//--- write.mlir
builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 1 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %bad = ac.memory %input entries 16 init 0 result_field "data" depth 1 latency 1 address {
  ^address(%item: !ac.var<!ac.struct<@types::@Request>>):
    %address = ac.var.get %item field "address" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i8>
    ac.memory.yield %address : !ac.var<i8>
  } write {
  ^write(%item: !ac.var<!ac.struct<@types::@Request>>):
    %bad_write = ac.var.get %item field "address" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i8>
    ac.memory.yield %bad_write : !ac.var<i8>
  } data {
  ^data(%item: !ac.var<!ac.struct<@types::@Request>>):
    %data = ac.var.get %item field "data" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i16>
    ac.memory.yield %data : !ac.var<i16>
  } : !ac.queue<!ac.struct<@types::@Request>> -> !ac.queue<!ac.struct<@types::@Request>>
}

//--- data.mlir
builtin.module attributes {ac.contract_epoch = "0.2"} {
  ac.type_scope @types {
    ac.struct @Request fields [{name = "address", type = i8}, {name = "write", type = i1}, {name = "data", type = i16}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Request> = {abi_alignment = 2 : i64, endianness = "little", preferred_alignment = 2 : i64, size = 4 : i64}>}
  %input = ac.source depth 1 latency 1 : !ac.queue<!ac.struct<@types::@Request>>
  %bad = ac.memory %input entries 16 init 0 result_field "data" depth 1 latency 1 address {
  ^address(%item: !ac.var<!ac.struct<@types::@Request>>):
    %address = ac.var.get %item field "address" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i8>
    ac.memory.yield %address : !ac.var<i8>
  } write {
  ^write(%item: !ac.var<!ac.struct<@types::@Request>>):
    %write = ac.var.get %item field "write" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i1>
    ac.memory.yield %write : !ac.var<i1>
  } data {
  ^data(%item: !ac.var<!ac.struct<@types::@Request>>):
    %bad_data = ac.var.get %item field "address" : !ac.var<!ac.struct<@types::@Request>> -> !ac.var<i8>
    ac.memory.yield %bad_data : !ac.var<i8>
  } : !ac.queue<!ac.struct<@types::@Request>> -> !ac.queue<!ac.struct<@types::@Request>>
}
