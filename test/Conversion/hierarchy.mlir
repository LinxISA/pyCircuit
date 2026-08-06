// RUN: %acir_opt --convert-acir-to-acsim %s | %FileCheck %s

// Test: deeper hierarchy conversion with multiple levels.
// Top → A → X (leaf) and Top → B (leaf).
// Construction order must be DFS pre-order: ["Top.a", "Top.a.x", "Top.b"].

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Top() -> () static {} {
    %a = ac.instance @a of @A() static {} id "a" path "a" : () -> ()
    %b = ac.instance @b of @B() static {} id "b" path "b" : () -> ()
    ac.return
  }
  ac.module @A() -> () static {} {
    %x = ac.instance @x of @X() static {} id "x" path "x" : () -> ()
    ac.return
  }
  ac.module @B() -> () static {} {
    ac.return
  }
  ac.module @X() -> () static {} {
    ac.return
  }
}

// CHECK:      builtin.module attributes {ac.contract_epoch = "0.1"}
// CHECK-NEXT:   acsim.model @Top epoch "0.1" root @Top
// CHECK-SAME:     construction ["Top.a", "Top.a.x", "Top.b"]
// CHECK-SAME:     destruction ["Top.b", "Top.a.x", "Top.a"]
// CHECK:        acsim.module @A
// CHECK:          acsim.instance @x target @X
// CHECK:        acsim.module @B
// CHECK:        acsim.module @Top
// CHECK:          acsim.instance @a target @A
// CHECK:          acsim.instance @b target @B
// CHECK:        acsim.module @X
