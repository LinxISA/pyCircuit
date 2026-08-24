#ifndef ACIR_DIALECT_ACIR_ACIROPS_TD
#define ACIR_DIALECT_ACIR_ACIROPS_TD

include "acir/Dialect/ACIR/ACIRInterfaces.td"
include "acir/Dialect/ACIR/ACIRTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "mlir/Interfaces/DataLayoutInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class ACIR_Op<string mnemonic, list<Trait> traits = []>
    : Op<ACIR_Dialect, mnemonic, traits>;

def ACIR_TypeScopeOp : ACIR_Op<"type_scope", [
    Symbol, SymbolTable, SingleBlock, NoTerminator,
    DeclareOpInterfaceMethods<DataLayoutOpInterface>
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
}

def ACIR_TypeAliasOp : ACIR_Op<"type_alias", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$target);
  let hasVerifier = 1;
}

class ACIR_RecordDeclarationOp<string mnemonic>
    : ACIR_Op<mnemonic, [Symbol, ACIR_RecordLike]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields);
  let hasVerifier = 1;
}

def ACIR_StructOp : ACIR_RecordDeclarationOp<"struct">;

def ACIR_EnumOp : ACIR_Op<"enum", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$enumerants);
  let hasVerifier = 1;
}

def ACIR_UnionOp : ACIR_Op<"union", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields,
                       StrAttr:$discriminator);
  let hasVerifier = 1;
}

def ACIR_PacketOp : ACIR_Op<"packet", [Symbol, ACIR_RecordLike]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields);
  let hasVerifier = 1;
}

def ACIR_TransactionOp : ACIR_RecordDeclarationOp<"transaction">;

def ACIR_RecordCreateOp : ACIR_Op<"record.create", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$values, StrArrayAttr:$field_names);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_RecordGetOp : ACIR_Op<"record.get", [Pure]> {
  let arguments = (ins AnyType:$record, StrAttr:$field);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_RecordWithOp : ACIR_Op<"record.with", [Pure]> {
  let arguments = (ins AnyType:$record, AnyType:$value, StrAttr:$field);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_PacketSerializeOp : ACIR_Op<"packet.serialize", [Pure]> {
  let arguments = (ins AnyType:$packet_value, SymbolRefAttr:$packet);
  let results = (outs AnyType:$bytes);
  let hasVerifier = 1;
}

def ACIR_PacketDeserializeOp : ACIR_Op<"packet.deserialize", [Pure]> {
  let arguments = (ins AnyType:$bytes, SymbolRefAttr:$packet);
  let results = (outs AnyType:$packet_value);
  let hasVerifier = 1;
}

def ACIR_TransformOp : ACIR_Op<"transform", [SingleBlock]> {
  let arguments = (ins Variadic<ACIR_QueueType>:$inputs,
                       DenseI64ArrayAttr:$output_depths,
                       DenseI64ArrayAttr:$output_latencies);
  let results = (outs Variadic<ACIR_QueueType>:$outputs);
  let regions = (region SizedRegion<1>:$body);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $inputs `depths` $output_depths `latencies` $output_latencies
    $body attr-dict `:` functional-type($inputs, $outputs)
  }];
}

def ACIR_TransformYieldOp : ACIR_Op<"transform.yield", [
    Pure, Terminator, HasParent<"TransformOp">
  ]> {
  let arguments = (ins Variadic<ACIR_VarType>:$values);
  let assemblyFormat = "attr-dict ($values^ `:` type($values))?";
}

def ACIR_FiringOp : ACIR_Op<"firing", [
    SingleBlock, RecursiveMemoryEffects
  ]> {
  let arguments = (ins Variadic<ACIR_QueueType>:$queues);
  let regions = (region SizedRegion<1>:$body);
  let hasVerifier = 1;
  let assemblyFormat = [{
    `(` $queues `)` $body attr-dict `:` `(` type($queues) `)`
  }];
}

def ACIR_FiringYieldOp : ACIR_Op<"firing.yield", [
    Pure, Terminator, HasParent<"FiringOp">
  ]> {
  let assemblyFormat = "attr-dict";
}

class ACIR_QueueEffectOp<string mnemonic>
    : ACIR_Op<mnemonic, [
        HasParent<"FiringOp">,
        DeclareOpInterfaceMethods<MemoryEffectsOpInterface>
      ]>;

def ACIR_QueuePeekOp : ACIR_QueueEffectOp<"queue.peek"> {
  let arguments = (ins ACIR_QueueType:$queue);
  let results = (outs ACIR_VarType:$value);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $queue attr-dict `:` qualified(type($queue)) `->`
    qualified(type($value))
  }];
}

def ACIR_QueuePopOp : ACIR_QueueEffectOp<"queue.pop"> {
  let arguments = (ins ACIR_QueueType:$queue);
  let results = (outs ACIR_VarType:$value);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $queue attr-dict `:` qualified(type($queue)) `->`
    qualified(type($value))
  }];
}

def ACIR_QueuePushOp : ACIR_QueueEffectOp<"queue.push"> {
  let arguments = (ins ACIR_QueueType:$queue, ACIR_VarType:$value);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $queue `,` $value attr-dict `:` qualified(type($queue)) `,`
    qualified(type($value))
  }];
}

def ACIR_InterfaceOp : ACIR_Op<"interface", [
    Symbol, SymbolTable, SingleBlock, NoTerminator, IsolatedFromAbove,
    RegionKindInterface, HasOnlyGraphRegion, ACIR_InterfaceContainer
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name $body attr-dict";
}

def ACIR_ProtocolOp : ACIR_Op<"protocol", [
    Symbol, SymbolTable, SingleBlock, NoTerminator, IsolatedFromAbove,
    RegionKindInterface, HasOnlyGraphRegion, ACIR_ProtocolContainer
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name $body attr-dict";
}

def ACIR_RoleOp : ACIR_Op<"role", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$dual,
                       StrAttr:$cardinality);
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name `dual` $dual `cardinality` $cardinality attr-dict";
}

def ACIR_StateOp : ACIR_Op<"state", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, BoolAttr:$initial,
                       BoolAttr:$terminal);
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name `initial` $initial `terminal` $terminal attr-dict";
}

def ACIR_EventOp : ACIR_Op<"event", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$from,
                       FlatSymbolRefAttr:$to, TypeAttr:$payload,
                       StrAttr:$action);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `from` $from `to` $to `payload` $payload `action` $action
    attr-dict
  }];
}

def ACIR_TransitionOp : ACIR_Op<"transition", [SingleBlock, NoTerminator]> {
  let arguments = (ins FlatSymbolRefAttr:$source, FlatSymbolRefAttr:$target,
                       FlatSymbolRefAttr:$event,
                       OptionalAttr<I64Attr>:$priority,
                       DefaultValuedAttr<BoolAttr, "false">:$transfer,
                       DefaultValuedAttr<BoolAttr, "false">:$retain);
  let regions = (region AnyRegion:$guard);
  let hasVerifier = 1;
  let assemblyFormat = [{
    `from` $source `to` $target `on` $event (`priority` $priority^)?
    `transfer` $transfer `retain` $retain `guard` $guard attr-dict
  }];
}

def ACIR_GuaranteeOp : ACIR_Op<"guarantee", [Pure]> {
  let arguments = (ins StrAttr:$kind, AnyAttr:$value);
  let hasVerifier = 1;
  let assemblyFormat = "$kind `=` $value attr-dict";
}

def ACIR_PortOp : ACIR_Op<"port", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type,
                       FlatSymbolRefAttr:$from, FlatSymbolRefAttr:$to,
                       FlatSymbolRefAttr:$protocol_from,
                       FlatSymbolRefAttr:$protocol_to);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `:` $type `from` $from `to` $to
    `protocol_roles` $protocol_from `to` $protocol_to attr-dict
  }];
}

def ACIR_SystemOp : ACIR_Op<"system", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$root,
                       StrAttr:$root_name, I64Attr:$tick_epoch,
                       StrAttr:$tick_unit,
                       OptionalAttr<SymbolRefAttr>:$primary_workload,
                       DictionaryAttr:$seed_policy,
                       ArrayAttr:$instrumentation,
                       DictionaryAttr:$result_schema,
                       DefaultValuedAttr<BoolAttr, "false">:$selected);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `root` $root `as` $root_name `tick` $tick_epoch $tick_unit
    (`workload` $primary_workload^)? `seed` $seed_policy
    `instrumentation` $instrumentation `results` $result_schema
    (`selected` $selected^)? attr-dict
  }];
}

def ACIR_ModuleOp : ACIR_Op<"module", [
    Symbol, FunctionOpInterface, IsolatedFromAbove, SingleBlock,
    RegionKindInterface, HasOnlyGraphRegion
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       OptionalAttr<StrAttr>:$sym_visibility,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs,
                       DictionaryAttr:$static_params);
  let regions = (region AnyRegion:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name,
                                 "::mlir::FunctionType":$type,
                                 CArg<"::mlir::DictionaryAttr", "{}">:$staticParams), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addAttribute(getFunctionTypeAttrName($_state.name),
                        ::mlir::TypeAttr::get(type));
    $_state.addAttribute("static_params",
                        staticParams ? staticParams : $_builder.getDictionaryAttr({}));
    $_state.addRegion();
  }]>];
  let extraClassDeclaration = [{
    ::mlir::ArrayRef<::mlir::Type> getArgumentTypes() {
      return getFunctionType().getInputs();
    }
    ::mlir::ArrayRef<::mlir::Type> getResultTypes() {
      return getFunctionType().getResults();
    }
    ::mlir::Region *getCallableRegion() { return &getBody(); }
    bool isDeclaration() { return false; }
  }];
  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
}

class ACIR_ModuleDeclarationOp<string mnemonic>
    : ACIR_Op<mnemonic, [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       OptionalAttr<StrAttr>:$sym_visibility,
                       DictionaryAttr:$static_params,
                       DictionaryAttr:$binding);
  let hasVerifier = 1;
}

def ACIR_ModuleExternOp : ACIR_ModuleDeclarationOp<"module.extern"> {
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       OptionalAttr<StrAttr>:$sym_visibility,
                       DictionaryAttr:$static_params,
                       DictionaryAttr:$implementation);
  let assemblyFormat = [{
    $sym_name `:` $function_type `parameters` $static_params
    `implementation` $implementation attr-dict
  }];
}

def ACIR_ModuleGeneratedOp : ACIR_ModuleDeclarationOp<"module.generated"> {
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       OptionalAttr<StrAttr>:$sym_visibility,
                       DictionaryAttr:$static_params,
                       DictionaryAttr:$generator);
  let assemblyFormat = [{
    $sym_name `:` $function_type `parameters` $static_params
    `generator` $generator attr-dict
  }];
}

def ACIR_InstanceOp : ACIR_Op<"instance", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$inputs,
                       FlatSymbolRefAttr:$definition, SymbolNameAttr:$sym_name,
                       StrAttr:$stable_id, StrAttr:$path,
                       DictionaryAttr:$static_args);
  let results = (outs Variadic<AnyType>:$outputs);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `of` $definition `(` $inputs `)` `static` $static_args
    `id` $stable_id `path` $path attr-dict `:` functional-type($inputs, $outputs)
  }];
}

def ACIR_ArrayOp : ACIR_Op<"array", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$inputs,
                       FlatSymbolRefAttr:$definition, SymbolNameAttr:$sym_name,
                       StrAttr:$stable_id, StrAttr:$path,
                       DenseI64ArrayAttr:$shape, ArrayAttr:$static_args);
  let results = (outs Variadic<AnyType>:$outputs);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `of` $definition `shape` $shape `(` $inputs `)`
    `static` $static_args `id` $stable_id `path` $path attr-dict
    `:` functional-type($inputs, $outputs)
  }];
}

def ACIR_InstancesOp : ACIR_Op<"instances", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$inputs,
                       SymbolNameAttr:$sym_name, StrAttr:$stable_id,
                       StrAttr:$path, ArrayAttr:$definitions,
                       StrArrayAttr:$names, StrArrayAttr:$stable_ids,
                       StrArrayAttr:$paths,
                       TypeAttrOf<FunctionType>:$interface,
                       ArrayAttr:$static_args);
  let results = (outs Variadic<AnyType>:$outputs);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `definitions` $definitions `names` $names `ids` $stable_ids
    `paths` $paths `interface` $interface `(` $inputs `)`
    `static` $static_args `id` $stable_id `path` $path attr-dict
    `:` functional-type($inputs, $outputs)
  }];
}

def ACIR_ViewOp : ACIR_Op<"view", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$inputs,
                       SymbolNameAttr:$sym_name, StrAttr:$kind,
                       FlatSymbolRefArrayAttr:$source_producers,
                       ArrayAttr:$source_shapes,
                       OptionalAttr<I64Attr>:$axis,
                       DenseI64ArrayAttr:$indices,
                       DenseI64ArrayAttr:$shape);
  let results = (outs Variadic<AnyType>:$outputs);
  let extraClassDeclaration = [{
    ::mlir::LogicalResult verifyWithProducerIndex(
        const ::llvm::StringMap<::mlir::Operation *> &producerIndex);
  }];
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name $kind `(` $inputs `)` `producers` $source_producers
    `sources` $source_shapes (`axis` $axis^)?
    `indices` $indices `shape` $shape attr-dict
    `:` functional-type($inputs, $outputs)
  }];
}

class ACIR_OwnedStateOp<string mnemonic, list<Trait> traits = []>
    : ACIR_Op<mnemonic, !listconcat([
        DeclareOpInterfaceMethods<MemoryEffectsOpInterface>
      ], traits)>;

def ACIR_QueueOp : ACIR_OwnedStateOp<"queue"> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$stable_id,
                       StrAttr:$path, TypeAttr:$payload,
                       I64Attr:$entry_capacity,
                       OptionalAttr<I64Attr>:$byte_capacity,
                       StrAttr:$ordering, FlatSymbolRefAttr:$protocol,
                       StrAttr:$ownership,
                       OptionalAttr<DictionaryAttr>:$watermarks,
                       DefaultValuedAttr<I64Attr, "1">:$delay_ticks);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `payload` $payload `entries` $entry_capacity
    (`bytes` $byte_capacity^)? `ordering` $ordering `protocol` $protocol
    `ownership` $ownership `id` $stable_id `path` $path
    (`watermarks` $watermarks^)? attr-dict
  }];
}

def ACIR_EventQueueOp : ACIR_OwnedStateOp<"event_queue"> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$stable_id,
                       StrAttr:$path, TypeAttr:$payload, I64Attr:$capacity,
                       StrAttr:$ordering, FlatSymbolRefAttr:$time_domain,
                       DefaultValuedAttr<I64Attr, "1">:$delay_ticks);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `payload` $payload `capacity` $capacity `ordering` $ordering
    `domain` $time_domain `id` $stable_id `path` $path attr-dict
  }];
}

def ACIR_ResourceOp : ACIR_OwnedStateOp<"resource"> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$stable_id,
                       StrAttr:$path, I64Attr:$capacity,
                       I64Attr:$issue_width, I64Attr:$initiation_interval,
                       DictionaryAttr:$latency_model,
                       DictionaryAttr:$lifecycle, StrAttr:$ownership,
                       OptionalAttr<FlatSymbolRefAttr>:$arbitration_owner,
                       ArrayAttr:$transaction_classes,
                       DefaultValuedAttr<I64Attr, "1">:$delay_ticks);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `capacity` $capacity `issue_width` $issue_width
    `ii` $initiation_interval `latency` $latency_model
    `lifecycle` $lifecycle `ownership` $ownership
    (`arbiter` $arbitration_owner^)? `classes` $transaction_classes
    `id` $stable_id `path` $path attr-dict
  }];
}

def ACIR_AddressSpaceOp : ACIR_Op<"address_space"> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$stable_id,
                       StrAttr:$path, I64Attr:$address_width,
                       StrAttr:$address_unit,
                       OptionalAttr<AnyAttr>:$data_layout,
                       OptionalAttr<FlatSymbolRefAttr>:$parent,
                       OptionalAttr<DictionaryAttr>:$translation);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `width` $address_width `unit` $address_unit
    `id` $stable_id `path` $path (`layout` $data_layout^)?
    (`parent` $parent^ `translate` $translation)? attr-dict
  }];
}

def ACIR_AddressMapOp : ACIR_Op<"address_map"> {
  let arguments = (ins SymbolNameAttr:$sym_name,
                       FlatSymbolRefAttr:$source, ArrayAttr:$entries,
                       DictionaryAttr:$default_behavior);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `source` $source `entries` $entries
    `default` $default_behavior attr-dict
  }];
}

def ACIR_TimeDomainOp : ACIR_Op<"time_domain"> {
  let arguments = (ins SymbolNameAttr:$sym_name, I64Attr:$period,
                       I64Attr:$phase, I64Attr:$tick_scale,
                       OptionalAttr<FlatSymbolRefAttr>:$parent,
                       OptionalAttr<DictionaryAttr>:$bridge);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `period` $period `phase` $phase `scale` $tick_scale
    (`parent` $parent^ `bridge` $bridge)? attr-dict
  }];
}

class ACIR_RuntimeEffectOp<string mnemonic, list<Trait> traits = []>
    : ACIR_Op<mnemonic, !listconcat([
        DeclareOpInterfaceMethods<MemoryEffectsOpInterface>
      ], traits)>;

def ACIR_ProcessOp : ACIR_Op<"process", [
    IsolatedFromAbove, SingleBlock, RecursiveMemoryEffects, ACIR_ProcessRegion
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$kind,
                       Variadic<AnyType>:$captures);
  let regions = (region AnyRegion:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name,
                                 "::llvm::StringRef":$kind,
                                 "::mlir::ValueRange":$captures), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addAttribute("kind", $_builder.getStringAttr(kind));
    $_state.addOperands(captures);
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
  let assemblyFormat = [{
    $sym_name `kind` $kind (`captures` `(` $captures^ `:` type($captures) `)`)?
    $body attr-dict
  }];
}

def ACIR_TrySendOp : ACIR_RuntimeEffectOp<"try_send"> {
  let arguments = (ins AnyType:$value, FlatSymbolRefAttr:$queue);
  let results = (outs I1:$accepted);
  let hasVerifier = 1;
  let assemblyFormat = "$queue $value attr-dict `:` type($value)";
}

def ACIR_TryRecvOp : ACIR_RuntimeEffectOp<"try_recv"> {
  let arguments = (ins FlatSymbolRefAttr:$queue);
  let results = (outs AnyType:$value, I1:$received);
  let hasVerifier = 1;
  let assemblyFormat = "$queue attr-dict `:` type($value)";
}

def ACIR_ScheduleOp : ACIR_RuntimeEffectOp<"schedule"> {
  let arguments = (ins AnyType:$value, I64:$delay,
                       FlatSymbolRefAttr:$target);
  let hasVerifier = 1;
  let assemblyFormat = "$target $value `after` $delay attr-dict `:` type($value)";
}

def ACIR_WaitUntilOp : ACIR_RuntimeEffectOp<"wait_until"> {
  let arguments = (ins I1:$condition);
  let hasVerifier = 1;
  let assemblyFormat = "$condition attr-dict";
}

def ACIR_WaitForOp : ACIR_RuntimeEffectOp<"wait_for"> {
  let arguments = (ins FlatSymbolRefAttr:$resource);
  let hasVerifier = 1;
  let assemblyFormat = "$resource attr-dict";
}

def ACIR_AwaitEventOp : ACIR_RuntimeEffectOp<"await_event"> {
  let arguments = (ins FlatSymbolRefAttr:$event_queue);
  let hasVerifier = 1;
  let assemblyFormat = "$event_queue attr-dict";
}

def ACIR_YieldSimOp : ACIR_RuntimeEffectOp<"yield_sim", [Terminator]> {
  let hasVerifier = 1;
  let assemblyFormat = "attr-dict";
}

def ACIR_TraceOpenOp : ACIR_RuntimeEffectOp<"trace.open"> {
  let arguments = (ins StrAttr:$source);
  let results = (outs Index:$cursor);
  let hasVerifier = 1;
  let assemblyFormat = "`source` $source attr-dict";
}

def ACIR_TraceNextOp : ACIR_RuntimeEffectOp<"trace.next"> {
  let arguments = (ins Index:$input_cursor, StrAttr:$source);
  let results = (outs Index:$cursor, AnyType:$entry, I1:$advanced);
  let hasVerifier = 1;
  let assemblyFormat = "$input_cursor `from` `source` $source attr-dict `:` type($entry)";
}

def ACIR_TraceDecodeOp : ACIR_Op<"trace.decode", [Pure]> {
  let arguments = (ins AnyType:$entry);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
  let assemblyFormat = "$entry attr-dict `:` type($entry) `to` type($result)";
}

def ACIR_TraceEofOp : ACIR_RuntimeEffectOp<"trace.eof"> {
  let arguments = (ins Index:$input_cursor, StrAttr:$source);
  let results = (outs I1:$eof);
  let hasVerifier = 1;
  let assemblyFormat = "$input_cursor `from` `source` $source attr-dict";
}

def ACIR_TracePositionOp : ACIR_RuntimeEffectOp<"trace.position"> {
  let arguments = (ins Index:$input_cursor, StrAttr:$source);
  let results = (outs Index:$position);
  let hasVerifier = 1;
  let assemblyFormat = "$input_cursor `from` `source` $source attr-dict";
}

class ACIR_ContractOp<string mnemonic>
    : ACIR_RuntimeEffectOp<mnemonic, [ACIR_Observation]> {
  let arguments = (ins I1:$condition, StrAttr:$message);
  let hasVerifier = 1;
  let assemblyFormat = "$condition `,` $message attr-dict";
}

def ACIR_RequireOp : ACIR_ContractOp<"require">;
def ACIR_EnsureOp : ACIR_ContractOp<"ensure">;
def ACIR_AssertOp : ACIR_ContractOp<"assert">;

def ACIR_ProbeOp : ACIR_RuntimeEffectOp<"probe", [ACIR_Observation]> {
  let arguments = (ins FlatSymbolRefAttr:$target, StrAttr:$kind);
  let results = (outs AnyType:$value);
  let hasVerifier = 1;
  let assemblyFormat = "$target `kind` $kind attr-dict `:` type($value)";
}

def ACIR_StatOp : ACIR_RuntimeEffectOp<"stat", [ACIR_Observation]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrAttr:$kind);
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name `kind` $kind attr-dict";
}

def ACIR_StatAddOp : ACIR_RuntimeEffectOp<"stat.add", [ACIR_Observation]> {
  let arguments = (ins AnyType:$value, FlatSymbolRefAttr:$stat);
  let hasVerifier = 1;
  let assemblyFormat = "$stat $value attr-dict `:` type($value)";
}

def ACIR_InstrumentationOp : ACIR_Op<"instrumentation", [
    SingleBlock, NoTerminator, RecursiveMemoryEffects,
    ACIR_Observation
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region AnyRegion:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
  let assemblyFormat = "$sym_name $body attr-dict";
}

def ACIR_ReturnOp : ACIR_Op<"return", [Pure, Terminator, ReturnLike,
                                        HasParent<"ModuleOp">]> {
  let arguments = (ins Variadic<AnyType>:$operands);
  let assemblyFormat = "attr-dict ($operands^ `:` type($operands))?";
  let hasVerifier = 1;
}

#endif
