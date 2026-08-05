// RUN: %split_file %s %t
// RUN: %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=512 %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=DEFAULT
// RUN: %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=512 --normalize-ac-file %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=REGISTERED
// RUN: %not %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=513 %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=DEPTH
// RUN: %not %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=513 --normalize-ac-file %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=DEPTH
// RUN: %not %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=10000 --acir-test-raw-malformed %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=DEPTH
// RUN: %not %acir_opt --verify-each=false --acir-test-pass-trace --acir-test-raw-depth=10000 --acir-test-raw-malformed --normalize-ac-file %t/shallow.mlir -o /dev/null 2>&1 | %FileCheck %s --check-prefix=DEPTH

// DEFAULT: enter:acir-test-materialize-raw-depth
// DEFAULT-NEXT: complete:acir-test-materialize-raw-depth
// DEFAULT-NEXT: enter:normalize-ac-file
// DEFAULT-NEXT: complete:normalize-ac-file
// DEFAULT-NEXT: enter:verify-ac-file
// DEFAULT-NEXT: complete:verify-ac-file
// DEFAULT-NEXT: enter:ac-verify-model
// DEFAULT-NEXT: complete:ac-verify-model

// REGISTERED: enter:acir-test-materialize-raw-depth
// REGISTERED-NEXT: complete:acir-test-materialize-raw-depth
// REGISTERED-NEXT: enter:normalize-ac-file
// REGISTERED-NEXT: complete:normalize-ac-file
// REGISTERED-NEXT: enter:verify-ac-file
// REGISTERED-NEXT: complete:verify-ac-file
// REGISTERED-NEXT: enter:normalize-ac-file
// REGISTERED-NEXT: complete:normalize-ac-file
// REGISTERED-NEXT: enter:ac-verify-model
// REGISTERED-NEXT: complete:ac-verify-model

// DEPTH: enter:acir-test-materialize-raw-depth
// DEPTH-NEXT: complete:acir-test-materialize-raw-depth
// DEPTH-NEXT: enter:normalize-ac-file
// DEPTH-NEXT: {{.*}}error: whole-model region nesting exceeds ACIR v0.1 capability limit 512
// DEPTH-NEXT: builtin.module attributes {ac.contract_epoch = "0.1"} {}
// DEPTH-NEXT: ^
// DEPTH-NEXT: fail:normalize-ac-file
// DEPTH-NOT: enter:

//--- shallow.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {}
