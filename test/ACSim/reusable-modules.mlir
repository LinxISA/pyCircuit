// RUN: %acir_opt_public %s | %FileCheck %s
// RUN: %acir_opt_public --emit-bytecode -o %t.bc %s
// RUN: %acir_opt_public %t.bc | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  acsim.model @reused epoch "0.1" root @Top
      construction ["Top.left", "Top.left.child", "Top.right", "Top.right.child"]
      destruction ["Top.right.child", "Top.right", "Top.left.child", "Top.left"]
      fingerprints {
        frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000001",
        binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000002",
        provider = "sha256:0000000000000000000000000000000000000000000000000000000000000003",
        profile = "sha256:0000000000000000000000000000000000000000000000000000000000000004",
        toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000005",
        schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000006"
      } {
    acsim.type @impl cpp "Component" kind "implementation" fingerprint "sha256:1000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @provider cpp "Provider" kind "provider" fingerprint "sha256:2000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @schema cpp "schema" kind "schema" fingerprint "sha256:3000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @value cpp "bool" kind "value" fingerprint "sha256:4000000000000000000000000000000000000000000000000000000000000000"

    acsim.binding @child_binding record {
      activation_sources = [], availability = "available", binding = "child_binding",
      binding_schema = "acsim-binding-0.1", component_schema = @schema,
      component_schema_fingerprint = "sha256:3000000000000000000000000000000000000000000000000000000000000000",
      construction = {arguments = [], kind = "constructor"}, contract_epoch = "0.1",
      cpp = {concept = "StatefulComponent", entry_points = {pure = "", reset = "child_reset", validate = "child_validate", work = "child_work", xfer = "child_xfer"}, header = "child.hpp", symbol = "Child", target = "model"},
      cpp_type = @value, effect = "stateful", fingerprint = "sha256:5000000000000000000000000000000000000000000000000000000000000000",
      implementation = @impl, ownership = {kind = "unique", placement = "member_or_array"},
      parameters = [], ports = [], provider = @provider,
      provider_implementation_fingerprint = "sha256:1000000000000000000000000000000000000000000000000000000000000000",
      resources = [], results = []
    }
    acsim.binding @leaf record {
      activation_sources = [], availability = "available", binding = "leaf",
      binding_schema = "acsim-binding-0.1", component_schema = @schema,
      component_schema_fingerprint = "sha256:3000000000000000000000000000000000000000000000000000000000000000",
      construction = {arguments = [2 : i64], kind = "constructor"}, contract_epoch = "0.1",
      cpp = {concept = "StatefulComponent", entry_points = {pure = "", reset = "leaf_reset", validate = "leaf_validate", work = "leaf_work", xfer = "leaf_xfer"}, header = "leaf.hpp", symbol = "Leaf", target = "model"},
      cpp_type = @value, effect = "stateful", fingerprint = "sha256:6000000000000000000000000000000000000000000000000000000000000000",
      implementation = @impl, ownership = {kind = "unique", placement = "member_or_array"},
      parameters = [{acir_type = "i64", cpp_type = "uint64_t", mapping = "template_argument", name = "depth", ordinal = 0 : i64, value = 2 : i64}], ports = [], provider = @provider,
      provider_implementation_fingerprint = "sha256:1000000000000000000000000000000000000000000000000000000000000000",
      resources = [], results = []
    }
    acsim.binding @top record {
      activation_sources = [], availability = "available", binding = "top",
      binding_schema = "acsim-binding-0.1", component_schema = @schema,
      component_schema_fingerprint = "sha256:3000000000000000000000000000000000000000000000000000000000000000",
      construction = {arguments = [], kind = "constructor"}, contract_epoch = "0.1",
      cpp = {concept = "StatefulComponent", entry_points = {pure = "", reset = "top_reset", validate = "top_validate", work = "top_work", xfer = "top_xfer"}, header = "top.hpp", symbol = "Top", target = "model"},
      cpp_type = @value, effect = "stateful", fingerprint = "sha256:7000000000000000000000000000000000000000000000000000000000000000",
      implementation = @impl, ownership = {kind = "unique", placement = "root_or_process"},
      parameters = [], ports = [], provider = @provider,
      provider_implementation_fingerprint = "sha256:1000000000000000000000000000000000000000000000000000000000000000",
      resources = [], results = []
    }

    acsim.module @Leaf binding @leaf static [2 : i64] specialization "sha256:8000000000000000000000000000000000000000000000000000000000000000" exports [] {
      %child = acsim.instance @child binding @child_binding target @child_binding args [] specialization "sha256:9000000000000000000000000000000000000000000000000000000000000000"
        : !acsim.owner<@child_binding>
      acsim.return
    }
    acsim.module @Top binding @top static [] specialization "sha256:a000000000000000000000000000000000000000000000000000000000000000" exports [] {
      %left = acsim.instance @left binding @leaf target @Leaf args [2 : i64] specialization "sha256:8000000000000000000000000000000000000000000000000000000000000000"
        : !acsim.owner<@leaf>
      %right = acsim.instance @right binding @leaf target @Leaf args [2 : i64] specialization "sha256:8000000000000000000000000000000000000000000000000000000000000000"
        : !acsim.owner<@leaf>
      acsim.return
    }

    %obj0, %act0 = acsim.dispatch @Top::@left path "Top.left" indices [] object 0 activation 0
      work "leaf_work" xfer "leaf_xfer" reset "leaf_reset" validate "leaf_validate"
      : !acsim.object_id, !acsim.activation_id
    %obj1, %act1 = acsim.dispatch @Leaf::@child path "Top.left.child" indices [] object 1 activation 1
      work "child_work" xfer "child_xfer" reset "child_reset" validate "child_validate"
      : !acsim.object_id, !acsim.activation_id
    %obj2, %act2 = acsim.dispatch @Top::@right path "Top.right" indices [] object 2 activation 2
      work "leaf_work" xfer "leaf_xfer" reset "leaf_reset" validate "leaf_validate"
      : !acsim.object_id, !acsim.activation_id
    %obj3, %act3 = acsim.dispatch @Leaf::@child path "Top.right.child" indices [] object 3 activation 3
      work "child_work" xfer "child_xfer" reset "child_reset" validate "child_validate"
      : !acsim.object_id, !acsim.activation_id
    acsim.activate %act0 to %obj0 : !acsim.activation_id to !acsim.object_id
    acsim.activate %act1 to %obj1 : !acsim.activation_id to !acsim.object_id
    acsim.activate %act2 to %obj2 : !acsim.activation_id to !acsim.object_id
    acsim.activate %act3 to %obj3 : !acsim.activation_id to !acsim.object_id
  }
}

// CHECK: acsim.module @Leaf
// CHECK: acsim.module @Top
// CHECK: acsim.dispatch @Leaf::@child path "Top.left.child"
// CHECK: acsim.dispatch @Leaf::@child path "Top.right.child"
