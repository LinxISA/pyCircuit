// RUN: %split_file %s %t
// RUN: %not %acir_opt_public %t/generic-spelling.mlir 2>&1 | %FileCheck %s --check-prefix=GENERIC
// RUN: %not %acir_opt %t/wrong-epoch.mlir 2>&1 | %FileCheck %s --check-prefix=EPOCH
// RUN: %not %acir_opt %t/two-models.mlir 2>&1 | %FileCheck %s --check-prefix=MODEL-COUNT
// RUN: %not %acir_opt %t/bad-fingerprints.mlir 2>&1 | %FileCheck %s --check-prefix=FINGERPRINTS
// RUN: %not %acir_opt %t/illegal-nested-acir.mlir 2>&1 | %FileCheck %s --check-prefix=CLOSED
// RUN: %not %acir_opt %t/illegal-process-scf.mlir 2>&1 | %FileCheck %s --check-prefix=PROCESS-CLOSED
// RUN: %not %acir_opt %t/unresolved-root.mlir 2>&1 | %FileCheck %s --check-prefix=ROOT
// RUN: %not %acir_opt %t/nonreverse-destruction.mlir 2>&1 | %FileCheck %s --check-prefix=DESTRUCTION
// RUN: %not %acir_opt %t/orphan-acsim-op.mlir 2>&1 | %FileCheck %s --check-prefix=ZERO-MODEL
// RUN: %not %acir_opt %t/nested-orphan-acsim-op.mlir 2>&1 | %FileCheck %s --check-prefix=NESTED-ZERO-MODEL
// RUN: %not %acir_opt %t/legacy-module-binding.mlir 2>&1 | %FileCheck %s --check-prefix=LEGACY-MODULE
// RUN: %not %acir_opt %t/legacy-placement-binding.mlir 2>&1 | %FileCheck %s --check-prefix=LEGACY-PLACEMENT
// RUN: %not %acir_opt %t/legacy-process-binding.mlir 2>&1 | %FileCheck %s --check-prefix=LEGACY-PROCESS

// GENERIC: error: generic ACIR operation spelling is internal-only
// EPOCH: contract epoch must be exactly "0.1"
// MODEL-COUNT: canonical ACSim requires exactly one acsim.model
// FINGERPRINTS: fingerprints must contain exactly frozen_acir, binding_lock, provider, profile, toolchain, and schema_set
// CLOSED: operation 'ac.system' is not legal in canonical ACSim
// PROCESS-CLOSED: operation 'scf.yield' is not legal in an acsim.process body
// ROOT: root reference '@missing' is unresolved
// DESTRUCTION: destruction order must be the exact reverse of construction order
// ZERO-MODEL: canonical ACSim requires exactly one acsim.model
// NESTED-ZERO-MODEL: canonical ACSim requires exactly one acsim.model
// LEGACY-MODULE: custom op 'acsim.module' expected 'interface'
// LEGACY-PLACEMENT: custom op 'acsim.instance' expected 'target'
// LEGACY-PROCESS: custom op 'acsim.process' expected 'captures'

//--- generic-spelling.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() ({}) {sym_name = "m", contract_epoch = "0.1"} : () -> ()
}

// The remaining split cases deliberately use the generic parser through the
// internal test entrypoint so malformed regions reach the real verifiers.

//--- wrong-epoch.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "m", contract_epoch = "0.2", root = @missing,
    construction_order = [], destruction_order = [], fingerprints = {}}>
    ({}) : () -> ()
}

//--- orphan-acsim-op.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.type"() <{sym_name = "v", cpp_name = "bool", kind = "value",
    fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}>
    : () -> ()
}

//--- nested-orphan-acsim-op.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  builtin.module @nested {
    acsim.type @v cpp "bool" kind "value" fingerprint "sha256:0000000000000000000000000000000000000000000000000000000000000000"
  }
}

//--- two-models.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "a", contract_epoch = "0.1", root = @missing,
    construction_order = [], destruction_order = [], fingerprints = {}}>
    ({}) : () -> ()
  "acsim.model"() <{sym_name = "b", contract_epoch = "0.1", root = @missing,
    construction_order = [], destruction_order = [], fingerprints = {}}>
    ({}) : () -> ()
}

//--- bad-fingerprints.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "m", contract_epoch = "0.1", root = @missing,
    construction_order = [], destruction_order = [], fingerprints = {frozen_acir = "x"}}>
    ({
      "acsim.type"() <{sym_name = "sentinel", cpp_name = "bool", kind = "value",
        fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}>
        : () -> ()
    }) : () -> ()
}

//--- illegal-nested-acir.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "m", contract_epoch = "0.1", root = @bad,
    construction_order = [], destruction_order = [], fingerprints = {
      frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      provider = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      profile = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}}>
  ({
    "ac.system"() <{sym_name = "bad", root = @bad, root_name = "bad",
      tick_epoch = 1 : i64, tick_unit = "cycles", seed_policy = {},
      instrumentation = [], result_schema = {}}> : () -> ()
  }) : () -> ()
}

//--- illegal-process-scf.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  acsim.model @m epoch "0.1" root @Top
      construction ["Top.p"] destruction ["Top.p"] fingerprints {
      frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      provider = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      profile = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000000"
    } {
    acsim.module @Top interface {ports = [], resources = [], results = []} static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
      acsim.process @p captures() names []
          entry @entry pcs [@entry] live [] fairness 1 specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" {
        state @entry {
          "scf.yield"() : () -> ()
        }
      }
      acsim.return
    }
  }
}

//--- unresolved-root.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "m", contract_epoch = "0.1", root = @missing,
    construction_order = [], destruction_order = [], fingerprints = {
      frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      provider = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      profile = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}}>
    ({
      "acsim.type"() <{sym_name = "sentinel", cpp_name = "bool", kind = "value",
        fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}>
        : () -> ()
    }) : () -> ()
}

//--- nonreverse-destruction.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "acsim.model"() <{sym_name = "m", contract_epoch = "0.1", root = @Top,
    construction_order = ["Top.a", "Top.b"],
    destruction_order = ["Top.a", "Top.b"], fingerprints = {
      frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      provider = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      profile = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}}>
    ({
      "acsim.module"() <{sym_name = "Top", interface = {ports = [], resources = [], results = []},
        static_params = [], specialization_fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000", exports = []}> ({
        %a = "acsim.instance"() <{sym_name = "a",
          target = @missing, static_args = [], specialization_fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}>
          : () -> !acsim.owner<@missing>
        %b = "acsim.instance"() <{sym_name = "b",
          target = @missing, static_args = [], specialization_fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000"}>
          : () -> !acsim.owner<@missing>
        "acsim.return"() : () -> ()
      }) : () -> ()
    }) : () -> ()
}

//--- legacy-module-binding.mlir
builtin.module {
  acsim.module @Top binding @legacy static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
    acsim.return
  }
}

//--- legacy-placement-binding.mlir
builtin.module {
  acsim.module @Top interface {ports = [], resources = [], results = []} static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
    %legacy = acsim.instance @legacy binding @legacy target @legacy args [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" : !acsim.owner<@legacy>
    acsim.return
  }
}

//--- legacy-process-binding.mlir
builtin.module {
  acsim.module @Top interface {ports = [], resources = [], results = []} static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
    acsim.process @legacy binding @legacy captures() names [] entry @entry pcs [@entry] live [] fairness 1 specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" {
      state @entry { acsim.terminate "success" }
    }
    acsim.return
  }
}
