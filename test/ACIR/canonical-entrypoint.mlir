// RUN: %split_file %s %t
// RUN: %acir_opt %t/generic.mlir > /dev/null
// RUN: %not %acir_opt_public %t/generic.mlir 2>&1 | %FileCheck %s --check-prefix=GENERIC
// RUN: %acir_opt_public %t/canonical.mlir | %FileCheck %s --check-prefix=CANONICAL
// RUN: %acir_opt %t/canonical.mlir --emit-bytecode -o %t/canonical.mlirbc
// RUN: %acir_opt_public %t/canonical.mlirbc > /dev/null
// RUN: %acir_opt %t/internal-provider.mlir > /dev/null
// RUN: %not %acir_opt_public %t/internal-provider.mlir 2>&1 | %FileCheck %s --check-prefix=PROVIDER

//--- generic.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.return"() : () -> ()
  }) : () -> ()
}
// GENERIC: generic ACIR operation spelling is internal-only

//--- canonical.mlir
module attributes {ac.contract_epoch = "0.1"} {
  // A quoted ACIR-like string is data, not a generic operation spelling.
  ac.module @Top() parameters {label = "ac.fake"} graph {
    ac.return
  }
}
// CANONICAL: ac.module @Top

//--- internal-provider.mlir
module attributes {ac.contract_epoch = "0.1"} {
  ac.module.extern @Leaf : () -> () parameters {}
      implementation {registry = "cpp", name = "Leaf"}
}
// PROVIDER: structural provider 'cpp:Leaf' is not registered
