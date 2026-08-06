// RUN: %acir_opt --convert-acir-to-acsim %s | %FileCheck %s

// Test: conversion of ac.array to acsim.array.
// Arrays lower with shape, target, and the ACSim array type.
// Construction order expands each array element individually.

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Top() -> () static {} {
    %arr = ac.array @lanes of @Leaf shape [2]() static {} id "lanes" path "lanes" : () -> ()
    ac.return
  }
  ac.module @Leaf() -> () static {} {
    ac.return
  }
}

// CHECK:      builtin.module attributes {ac.contract_epoch = "0.1"}
// CHECK-NEXT:   acsim.model @Top epoch "0.1" root @Top
// CHECK-SAME:     construction ["Top.lanes[0]", "Top.lanes[1]"]
// CHECK-SAME:     destruction ["Top.lanes[1]", "Top.lanes[0]"]
// CHECK:        acsim.module @Leaf
// CHECK:        acsim.module @Top
// CHECK:          acsim.array @lanes target @Leaf
// CHECK-SAME:       shape [2]
// CHECK-SAME:       : !acsim.array<[2], !acsim.owner<@Leaf>>
