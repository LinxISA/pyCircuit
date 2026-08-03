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
}

def ACIR_RoleOp : ACIR_Op<"role", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$dual,
                       StrAttr:$cardinality);
  let hasVerifier = 1;
}

def ACIR_StateOp : ACIR_Op<"state", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, BoolAttr:$initial,
                       BoolAttr:$terminal);
  let hasVerifier = 1;
}

def ACIR_EventOp : ACIR_Op<"event", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$from,
                       FlatSymbolRefAttr:$to, TypeAttr:$payload,
                       StrAttr:$action);
  let hasVerifier = 1;
}

def ACIR_TransitionOp : ACIR_Op<"transition", [SingleBlock, NoTerminator]> {
  let arguments = (ins FlatSymbolRefAttr:$source, FlatSymbolRefAttr:$target,
                       FlatSymbolRefAttr:$event,
                       OptionalAttr<I64Attr>:$priority,
                       DefaultValuedAttr<BoolAttr, "false">:$transfer,
                       DefaultValuedAttr<BoolAttr, "false">:$retain);
  let regions = (region AnyRegion:$guard);
  let hasVerifier = 1;
}

def ACIR_GuaranteeOp : ACIR_Op<"guarantee", [Pure]> {
  let arguments = (ins StrAttr:$kind, AnyAttr:$value);
  let hasVerifier = 1;
}

def ACIR_PortOp : ACIR_Op<"port", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type,
                       FlatSymbolRefAttr:$from, FlatSymbolRefAttr:$to,
                       FlatSymbolRefAttr:$protocol_from,
                       FlatSymbolRefAttr:$protocol_to);
  let hasVerifier = 1;
}

def ACIR_SystemOp : ACIR_Op<"system", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$root,
                       StrAttr:$root_name, I64Attr:$tick_epoch,
                       StrAttr:$tick_unit,
                       OptionalAttr<FlatSymbolRefAttr>:$primary_workload,
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

def ACIR_ReturnOp : ACIR_Op<"return", [Pure, Terminator, ReturnLike,
                                        HasParent<"ModuleOp">]> {
  let arguments = (ins Variadic<AnyType>:$operands);
  let assemblyFormat = "attr-dict ($operands^ `:` type($operands))?";
  let hasVerifier = 1;
}

#endif
