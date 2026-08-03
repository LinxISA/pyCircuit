// RUN: %acir_opt_public %s | %FileCheck %s
// RUN: %acir_opt_public %s | %acir_opt_public | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Bridge() parameters {} graph { ac.return }
  ac.module @Top() parameters {} graph {
    ac.instance @cdc of @Bridge() static {} id "cdc" path "cdc" : () -> ()
    ac.time_domain @global period 1 phase 0 scale 1
    ac.time_domain @core period 2 phase 1 scale 2 parent @global
        bridge {kind = "explicit", owner = @cdc}
    ac.address_space @physical width 48 unit "byte" id "physical" path "physical"
        layout #dlti.dl_spec<>
    ac.address_space @virtual width 32 unit "byte" id "virtual" path "virtual"
        parent @physical translate {numerator = 1 : i64, denominator = 1 : i64, offset = 4096 : i64}
    ac.address_map @map source @virtual entries [
      {base = 0 : i64, size = 4096 : i64, target = @physical, offset = 0 : i64,
       permissions = ["read", "write"], classes = [], priority = 1 : i64},
      {base = 4096 : i64, size = 4096 : i64, target = @physical, offset = 8192 : i64,
       permissions = ["read"], classes = [],
       interleave = {granularity = 64 : i64, banks = 4 : i64, bank = 0 : i64}}
    ] default {kind = "unmapped"}
    ac.return
  }
}

// CHECK: ac.time_domain @core
// CHECK: ac.address_space @virtual
// CHECK: ac.address_map @map
