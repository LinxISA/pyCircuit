// RUN: %not %acir_opt --convert-acir-to-acsim %s 2>&1 | %FileCheck %s --check-prefix=NOT-YET

// NOT-YET: ACIR-to-ACSim conversion
// After implementation, replace RUN line with:
// RUN: %acir_opt --convert-acir-to-acsim %s | %FileCheck %s

// Test: convert a simple frozen ACIR module with one instance to ACSim.
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

// After implementation, expected ACSim output:
// CHECK: acsim.model
// CHECK: acsim.module @Top
// CHECK: acsim.instance @child target @Child
