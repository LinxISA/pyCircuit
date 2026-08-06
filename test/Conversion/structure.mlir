// RUN: %not %acir_opt --convert-acir-to-acsim %s 2>&1 | %FileCheck %s --check-prefix=NOT-YET

// NOT-YET: ACIR-to-ACSim conversion
// After implementation, replace the RUN line above with a positive
// pipeline: run --convert-acir-to-acsim on this file and check the output
// with the default FileCheck prefix against the expectations below.

// Test: convert a simple frozen ACIR module with one instance to ACSim.
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Child() parameters {} graph {
    ac.process @dummy kind "control" { ac.yield_sim }
    ac.return
  }
  ac.module @Top() parameters {} graph {
    "ac.instance"() <{definition = @Child, sym_name = "child", stable_id = "child", path = "child", static_args = {}}> : () -> ()
    ac.return
  }
}

// After implementation, expected ACSim output:
// CHECK: acsim.model
// CHECK: acsim.module @Top
// CHECK: acsim.instance @child target @Child
