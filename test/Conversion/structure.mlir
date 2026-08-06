// RUN: %acir_opt --convert-acir-to-acsim %s | %FileCheck %s

// Test: convert a simple frozen ACIR module with one instance to ACSim.
// The input has two modules — Child (leaf) and Top (root with one child instance).
// The conversion must produce a valid acsim.model wrapper with correct
// construction/destruction order and instance lowering.

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Child() -> () static {} {
    ac.process @dummy kind "control" { ac.yield_sim }
    ac.return
  }
  ac.module @Top() -> () static {} {
    %inst = ac.instance @child of @Child() static {} id "child" path "child" : () -> ()
    ac.return
  }
}

// CHECK:      builtin.module attributes {ac.contract_epoch = "0.1"}
// CHECK-NEXT:   acsim.model @Top epoch "0.1" root @Top
// CHECK:        acsim.module @Child
// CHECK:        acsim.module @Top
// CHECK:          acsim.instance @child target @Child
// CHECK:          acsim.return
