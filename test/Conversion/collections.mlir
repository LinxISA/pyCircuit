// RUN: %acir_opt --convert-acir-to-acsim %s | %FileCheck %s

// Test: conversion of ac.module.extern and ac.module.generated
// These declaration-only modules become acsim.module ops with empty bodies.

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Top() -> () static {} {
    ac.return
  }
  ac.module.extern @Ext : (i32) -> i32 parameters {} implementation {registry = "cpp", name = "Ext"}
  ac.module.generated @Gen : (i32) -> i32 parameters {} generator {registry = "ac", name = "Gen"}
}

// CHECK:      builtin.module attributes {ac.contract_epoch = "0.1"}
// CHECK-NEXT:   acsim.model @Top epoch "0.1" root @Top
// CHECK:        acsim.type @int32_t cpp "int32_t" kind "value"
// CHECK:        acsim.module @Ext
// CHECK:          acsim.return
// CHECK:        acsim.module @Gen
// CHECK:          acsim.return
// CHECK:        acsim.module @Top
// CHECK:          acsim.return
