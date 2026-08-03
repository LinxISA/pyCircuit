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
  EXPECT_TRUE(RoleOp::create(builder, loc, "b", "a", "exclusive"));
  EXPECT_TRUE(StateOp::create(builder, loc, "s", true, false));
  EXPECT_TRUE(EventOp::create(builder, loc, "e", "a", "b", builder.getI8Type(),
                              "notify"));
  auto transition =
      TransitionOp::create(builder, loc, "s", "s", "e", nullptr, false, false);
  transition.getGuard().emplaceBlock();
  EXPECT_TRUE(transition);
  EXPECT_TRUE(GuaranteeOp::create(builder, loc, "ordering",
                                  builder.getStringAttr("fifo")));

  builder.setInsertionPointAfter(protocol);
  auto interface = InterfaceOp::create(builder, loc, "I");
  builder.setInsertionPointToStart(&interface.getBody().emplaceBlock());
  EXPECT_TRUE(RoleOp::create(builder, loc, "source", "sink", "exclusive"));
  EXPECT_TRUE(RoleOp::create(builder, loc, "sink", "source", "exclusive"));
  auto channel = ChannelType::get(&context, builder.getI8Type(),
                                  mlir::FlatSymbolRefAttr::get(&context, "p"));
  EXPECT_TRUE(PortOp::create(builder, loc, "data", channel, "source", "sink"));
  EXPECT_TRUE(mlir::isa<ProtocolContainerOpInterface>(protocol.getOperation()));
  EXPECT_TRUE(
      mlir::isa<InterfaceContainerOpInterface>(interface.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::RegionKindInterface>(protocol.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::RegionKindInterface>(interface.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(roleA.getOperation()));
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
