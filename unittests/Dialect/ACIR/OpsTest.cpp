#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/InitAllDialects.h"

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Parser/Parser.h"
#include "gtest/gtest.h"

#include <array>
#include <chrono>

namespace acir::ac {
namespace {

TEST(ACIROpsTest, PublicBuildersConstructEveryTaskFourOperation) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());

  auto scope = TypeScopeOp::create(builder, loc, "types");
  builder.setInsertionPointToStart(&scope.getBody().emplaceBlock());
  auto names = builder.getStrArrayAttr({"x"});
  auto field = builder.getDictionaryAttr({
      builder.getNamedAttr("name", builder.getStringAttr("x")),
      builder.getNamedAttr("type", mlir::TypeAttr::get(builder.getI8Type())),
  });
  auto fields = builder.getArrayAttr({field});

  EXPECT_TRUE(TypeAliasOp::create(builder, loc, "Byte", builder.getI8Type()));
  EXPECT_TRUE(StructOp::create(builder, loc, "S", fields));
  EXPECT_TRUE(
      EnumOp::create(builder, loc, "E", builder.getStrArrayAttr({"a"})));
  EXPECT_TRUE(UnionOp::create(builder, loc, "U", fields, "x"));
  EXPECT_TRUE(PacketOp::create(builder, loc, "P", fields));
  EXPECT_TRUE(TransactionOp::create(builder, loc, "T", fields));

  auto input = mlir::UnrealizedConversionCastOp::create(
                   builder, loc, mlir::TypeRange{builder.getI8Type()},
                   mlir::ValueRange{})
                   .getResult(0);
  auto structRef = mlir::SymbolRefAttr::get(
      &context, "types", {mlir::FlatSymbolRefAttr::get(&context, "S")});
  auto packetRef = mlir::SymbolRefAttr::get(
      &context, "types", {mlir::FlatSymbolRefAttr::get(&context, "P")});
  auto structType = StructType::get(&context, structRef);
  auto packetType = PacketType::get(&context, packetRef);
  auto bytesType = VectorType::get(&context, 1, builder.getI8Type());
  auto record = RecordCreateOp::create(builder, loc, structType,
                                       mlir::ValueRange{input}, names);
  EXPECT_TRUE(record);
  auto get = RecordGetOp::create(builder, loc, builder.getI8Type(),
                                 record.getResult(), "x");
  auto with = RecordWithOp::create(builder, loc, structType, record.getResult(),
                                   input, "x");
  EXPECT_TRUE(get);
  EXPECT_TRUE(with);
  auto packet =
      mlir::UnrealizedConversionCastOp::create(
          builder, loc, mlir::TypeRange{packetType}, mlir::ValueRange{})
          .getResult(0);
  auto bytes =
      PacketSerializeOp::create(builder, loc, bytesType, packet, packetRef);
  EXPECT_TRUE(bytes);
  auto deserialize = PacketDeserializeOp::create(builder, loc, packetType,
                                                 bytes.getResult(), packetRef);
  EXPECT_TRUE(deserialize);
  EXPECT_TRUE(mlir::isMemoryEffectFree(record.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(get.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(with.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(bytes.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(deserialize.getOperation()));
}

TEST(ACIROpsTest, LayoutAndEffectsAreDeclaredThroughMLIRInterfaces) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());
  auto scope = TypeScopeOp::create(builder, loc, "types");
  EXPECT_TRUE(mlir::isa<mlir::DataLayoutOpInterface>(scope.getOperation()));
  mlir::DataLayout layout(scope);
  EXPECT_EQ(layout.getTypeSize(builder.getI32Type()).getFixedValue(), 4u);

  builder.setInsertionPointToStart(&scope.getBody().emplaceBlock());
  auto input = mlir::UnrealizedConversionCastOp::create(
      builder, loc, mlir::TypeRange{builder.getI8Type()}, mlir::ValueRange{});
  auto structRef = mlir::SymbolRefAttr::get(
      &context, "types", {mlir::FlatSymbolRefAttr::get(&context, "S")});
  auto packetRef = mlir::SymbolRefAttr::get(
      &context, "types", {mlir::FlatSymbolRefAttr::get(&context, "P")});
  auto structType = StructType::get(&context, structRef);
  auto record = RecordCreateOp::create(builder, loc, structType,
                                       mlir::ValueRange{input.getResult(0)},
                                       builder.getStrArrayAttr({"x"}));
  auto packetType = PacketType::get(&context, packetRef);
  auto packet = mlir::UnrealizedConversionCastOp::create(
      builder, loc, mlir::TypeRange{packetType}, mlir::ValueRange{});
  auto serialized = PacketSerializeOp::create(
      builder, loc, VectorType::get(&context, 1, builder.getI8Type()),
      packet.getResult(0), packetRef);
  EXPECT_TRUE(mlir::isMemoryEffectFree(record.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(serialized.getOperation()));
}

TEST(ACIROpsTest, ClosedRegistryDoesNotLoadUnrelatedDialects) {
  mlir::DialectRegistry registry;
  registry.insert<ACIRDialect, mlir::DLTIDialect>();
  mlir::MLIRContext context(registry);
  context.loadAllAvailableDialects();
  EXPECT_NE(context.getLoadedDialect("ac"), nullptr);
  EXPECT_NE(context.getLoadedDialect("dlti"), nullptr);
  EXPECT_EQ(context.getLoadedDialect("arith"), nullptr);
  EXPECT_EQ(context.getLoadedDialect("scf"), nullptr);
}

TEST(ACIROpsTest, QualifiedNamedTypesImplementDataLayout) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::Type type = mlir::parseType("!ac.struct<@types::@S>", &context);
  ASSERT_TRUE(type);
  EXPECT_TRUE(mlir::isa<mlir::DataLayoutTypeInterface>(type));
}

TEST(ACIROpsTest, NamedAggregateLayoutUsesExactDLTIEntry) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect, mlir::DLTIDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());
  auto scope = TypeScopeOp::create(builder, loc, "types");
  auto reference = mlir::SymbolRefAttr::get(
      &context, "types", {mlir::FlatSymbolRefAttr::get(&context, "S")});
  auto type = StructType::get(&context, reference);
  auto metadata = builder.getDictionaryAttr({
      builder.getNamedAttr("size", builder.getI64IntegerAttr(12)),
      builder.getNamedAttr("abi_alignment", builder.getI64IntegerAttr(4)),
      builder.getNamedAttr("preferred_alignment", builder.getI64IntegerAttr(8)),
      builder.getNamedAttr("endianness", builder.getStringAttr("big")),
  });
  auto entry = mlir::DataLayoutEntryAttr::get(type, metadata);
  auto spec =
      mlir::DataLayoutSpecAttr::get(&context, mlir::DataLayoutEntryList{entry});
  scope->setAttr(mlir::DLTIDialect::kDataLayoutAttrName, spec);

  mlir::DataLayout layout(scope);
  EXPECT_EQ(layout.getTypeSize(type).getFixedValue(), 12u);
  EXPECT_EQ(layout.getTypeABIAlignment(type), 4u);
  EXPECT_EQ(layout.getTypePreferredAlignment(type), 8u);
  auto queried = spec.query(mlir::DataLayoutEntryKey(type));
  ASSERT_TRUE(mlir::succeeded(queried));
  EXPECT_EQ(mlir::cast<mlir::DictionaryAttr>(*queried)
                .getAs<mlir::StringAttr>("endianness")
                .getValue(),
            "big");
}

TEST(ACIROpsTest, PublicRegistryIncludesOnlyRequiredDLTIDependency) {
  mlir::DialectRegistry registry;
  acir::registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  context.loadAllAvailableDialects();
  EXPECT_NE(context.getLoadedDialect("dlti"), nullptr);
}

TEST(ACIROpsTest, PublicBuildersConstructEveryTaskFiveOperation) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());

  auto protocol = ProtocolOp::create(builder, loc, "p");
  builder.setInsertionPointToStart(&protocol.getBody().emplaceBlock());
  auto roleA = RoleOp::create(builder, loc, "a", "b", "exclusive");
  EXPECT_TRUE(roleA);
  auto roleB = RoleOp::create(builder, loc, "b", "a", "exclusive");
  auto state = StateOp::create(builder, loc, "s", true, false);
  auto event = EventOp::create(builder, loc, "e", "a", "b", builder.getI8Type(),
                               "notify");
  auto transition =
      TransitionOp::create(builder, loc, "s", "s", "e", nullptr, false, false);
  transition.getGuard().emplaceBlock();
  EXPECT_TRUE(transition);
  auto guarantee = GuaranteeOp::create(builder, loc, "ordering",
                                       builder.getStringAttr("fifo"));

  builder.setInsertionPointAfter(protocol);
  auto interface = InterfaceOp::create(builder, loc, "I");
  builder.setInsertionPointToStart(&interface.getBody().emplaceBlock());
  EXPECT_TRUE(RoleOp::create(builder, loc, "a", "b", "exclusive"));
  EXPECT_TRUE(RoleOp::create(builder, loc, "b", "a", "exclusive"));
  auto channel = ChannelType::get(&context, builder.getI8Type(),
                                  mlir::FlatSymbolRefAttr::get(&context, "p"));
  auto port = PortOp::create(builder, loc, "data", channel, "a", "b", "a", "b");
  EXPECT_TRUE(port);
  EXPECT_EQ(port.getProtocolFromAttr().getValue(), "a");
  EXPECT_EQ(port.getProtocolToAttr().getValue(), "b");
  EXPECT_TRUE(mlir::isa<ProtocolContainerOpInterface>(protocol.getOperation()));
  EXPECT_TRUE(
      mlir::isa<InterfaceContainerOpInterface>(interface.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::RegionKindInterface>(protocol.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::RegionKindInterface>(interface.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(protocol.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(interface.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(roleA.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(roleB.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(state.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(event.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::SymbolOpInterface>(port.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(roleA.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(roleB.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(state.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(event.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(guarantee.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(port.getOperation()));
  EXPECT_TRUE(mlir::succeeded(mlir::verify(module)));
}

TEST(ACIROpsTest, TaskFiveRegistryContainsExactlyTheRequiredNewOperations) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  const std::array<llvm::StringLiteral, 8> names = {
      "ac.interface", "ac.protocol",   "ac.role",      "ac.state",
      "ac.event",     "ac.transition", "ac.guarantee", "ac.port",
  };
  for (llvm::StringLiteral name : names)
    EXPECT_TRUE(mlir::OperationName(name, &context).isRegistered())
        << name.str();
  EXPECT_FALSE(mlir::OperationName("ac.connect", &context).isRegistered());
  EXPECT_FALSE(mlir::OperationName("ac.ready_valid", &context).isRegistered());

  std::vector<std::string> actual;
  for (mlir::RegisteredOperationName operation :
       context.getRegisteredOperationsByDialect("ac"))
    actual.push_back(operation.getStringRef().str());
  llvm::sort(actual);
  std::vector<std::string> expected = {
      "ac.enum",
      "ac.event",
      "ac.guarantee",
      "ac.interface",
      "ac.packet",
      "ac.packet.deserialize",
      "ac.packet.serialize",
      "ac.port",
      "ac.protocol",
      "ac.record.create",
      "ac.record.get",
      "ac.record.with",
      "ac.role",
      "ac.state",
      "ac.struct",
      "ac.transaction",
      "ac.transition",
      "ac.type_alias",
      "ac.type_scope",
      "ac.union",
  };
  llvm::sort(expected);
  EXPECT_EQ(actual, expected);
}

TEST(ACIROpsTest, TopologyVerifierWalksTypeAttributesAndLocations) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::ScopedDiagnosticHandler handler(
      &context, [](mlir::Diagnostic &) { return mlir::success(); });
  mlir::OpBuilder builder(&context);
  auto protocol = mlir::FlatSymbolRefAttr::get(&context, "missing");
  auto flow = FlowType::get(&context, builder.getI8Type(), protocol);
  auto nested = OptionalType::get(&context, flow);

  auto attributeModule = mlir::ModuleOp::create(builder.getUnknownLoc());
  attributeModule->setAttr("metadata", mlir::TypeAttr::get(nested));
  EXPECT_TRUE(mlir::failed(verifyTopologyTypeUses(attributeModule)));

  auto operandModule = mlir::ModuleOp::create(builder.getUnknownLoc());
  builder.setInsertionPointToStart(operandModule.getBody());
  auto source = mlir::UnrealizedConversionCastOp::create(
      builder, builder.getUnknownLoc(), mlir::TypeRange{nested},
      mlir::ValueRange{});
  auto consumer = mlir::UnrealizedConversionCastOp::create(
      builder, builder.getUnknownLoc(), mlir::TypeRange{builder.getI1Type()},
      source.getResults());
  EXPECT_TRUE(mlir::failed(verifyTopologyTypeUses(consumer)));

  auto location = mlir::FusedLoc::get(&context, {builder.getUnknownLoc()},
                                      mlir::TypeAttr::get(nested));
  auto locationModule = mlir::ModuleOp::create(location);
  EXPECT_TRUE(mlir::failed(verifyTopologyTypeUses(locationModule)));
}

TEST(ACIROpsTest, ReverseChainOwnershipAnalysisHasLinearScaling) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());
  auto protocol = ProtocolOp::create(builder, loc, "long_chain");
  builder.setInsertionPointToStart(&protocol.getBody().emplaceBlock());
  RoleOp::create(builder, loc, "a", "b", "exclusive");
  RoleOp::create(builder, loc, "b", "a", "exclusive");

  constexpr unsigned stateCount = 1201;
  std::vector<std::string> stateNames;
  stateNames.reserve(stateCount);
  for (unsigned index = 0; index < stateCount; ++index) {
    stateNames.push_back((llvm::Twine("s") + llvm::Twine(index)).str());
    StateOp::create(builder, loc, stateNames.back(), index == 0,
                    index + 1 == stateCount);
  }
  EventOp::create(builder, loc, "step", "a", "b", builder.getI8Type(),
                  "notify");
  for (unsigned index = stateCount - 1; index > 0; --index) {
    auto transition =
        TransitionOp::create(builder, loc, stateNames[index - 1],
                             stateNames[index], "step", nullptr, false, false);
    transition.getGuard().emplaceBlock();
  }

  auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE(mlir::succeeded(mlir::verify(module)));
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(ACIROpsTest, TransitionTableRejectsAmbiguousRowsDeterministically) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module {
      "ac.protocol"() <{sym_name = "p"}> ({
        "ac.role"() <{sym_name = "a", dual = @b, cardinality = "exclusive"}> : () -> ()
        "ac.role"() <{sym_name = "b", dual = @a, cardinality = "exclusive"}> : () -> ()
        "ac.state"() <{sym_name = "s", initial = true, terminal = false}> : () -> ()
        "ac.event"() <{sym_name = "e", from = @a, to = @b, payload = i8, action = "notify"}> : () -> ()
        "ac.transition"() <{source = @s, target = @s, event = @e}> ({}) : () -> ()
        "ac.transition"() <{source = @s, target = @s, event = @e}> ({}) : () -> ()
      }) : () -> ()
    }
  )mlir";
  std::string diagnostic;
  mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic &value) {
    llvm::raw_string_ostream(diagnostic) << value;
    return mlir::success();
  });
  EXPECT_FALSE(mlir::parseSourceString<mlir::ModuleOp>(source, &context));
  EXPECT_NE(
      diagnostic.find("overlapping transitions require explicit priority"),
      std::string::npos);
}

} // namespace
} // namespace acir::ac
