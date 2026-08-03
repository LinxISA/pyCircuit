#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/GraphRegion.h"
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
      "ac.array",
      "ac.enum",
      "ac.event",
      "ac.guarantee",
      "ac.interface",
      "ac.instance",
      "ac.instances",
      "ac.module",
      "ac.module.extern",
      "ac.module.generated",
      "ac.packet",
      "ac.packet.deserialize",
      "ac.packet.serialize",
      "ac.port",
      "ac.protocol",
      "ac.record.create",
      "ac.record.get",
      "ac.record.with",
      "ac.return",
      "ac.role",
      "ac.state",
      "ac.struct",
      "ac.system",
      "ac.transaction",
      "ac.transition",
      "ac.type_alias",
      "ac.type_scope",
      "ac.union",
      "ac.view",
  };
  llvm::sort(expected);
  EXPECT_EQ(actual, expected);
}

TEST(ACIROpsTest, PublicBuildersConstructEveryTaskSixOperation) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  getStructuralProviderRegistry(&context).registerExternal("Leaf");
  getStructuralProviderRegistry(&context).registerGenerator("Generated");
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto file = mlir::ModuleOp::create(loc);
  file->setAttr("ac.contract_epoch", builder.getStringAttr("0.1"));
  builder.setInsertionPointToStart(file.getBody());

  auto emptyType = builder.getFunctionType({}, {});
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto binding = builder.getDictionaryAttr({
      builder.getNamedAttr("registry", builder.getStringAttr("cpp")),
      builder.getNamedAttr("name", builder.getStringAttr("Leaf")),
  });
  auto generator = builder.getDictionaryAttr({
      builder.getNamedAttr("registry", builder.getStringAttr("ac")),
      builder.getNamedAttr("name", builder.getStringAttr("Generated")),
  });
  auto leaf =
      ModuleExternOp::create(builder, loc, "Leaf", emptyType,
                             mlir::StringAttr(), emptyDictionary, binding);
  auto generated =
      ModuleGeneratedOp::create(builder, loc, "Generated", emptyType,
                                mlir::StringAttr(), emptyDictionary, generator);
  EXPECT_TRUE(leaf);
  EXPECT_TRUE(generated);

  auto top = ModuleOp::create(builder, loc, "Top", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(top.addEntryBlock());
  auto instance =
      InstanceOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{},
                         "Leaf", "child", "child", "child", emptyDictionary);
  auto array = ArrayOp::create(
      builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "Leaf", "lanes",
      "lanes", "lanes", llvm::ArrayRef<int64_t>{2},
      builder.getArrayAttr({emptyDictionary, emptyDictionary}));
  auto definitions = builder.getArrayAttr({
      mlir::FlatSymbolRefAttr::get(&context, "Leaf"),
      mlir::FlatSymbolRefAttr::get(&context, "Generated"),
  });
  auto instances = InstancesOp::create(
      builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "mixed", "mixed",
      "mixed", definitions, builder.getStrArrayAttr({"a", "b"}),
      builder.getStrArrayAttr({"mix-a", "mix-b"}),
      builder.getStrArrayAttr({"mix_a", "mix_b"}), emptyType,
      builder.getArrayAttr({emptyDictionary, emptyDictionary}));
  auto view = ViewOp::create(
      builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "view",
      "permutation",
      builder.getArrayAttr({mlir::FlatSymbolRefAttr::get(&context, "child")}),
      builder.getArrayAttr({builder.getDenseI64ArrayAttr({0})}),
      mlir::IntegerAttr(), llvm::ArrayRef<int64_t>{},
      llvm::ArrayRef<int64_t>{0});
  auto returnOp = ReturnOp::create(builder, loc, mlir::ValueRange{});
  EXPECT_TRUE(instance);
  EXPECT_TRUE(array);
  EXPECT_TRUE(instances);
  EXPECT_TRUE(view);
  EXPECT_TRUE(returnOp);

  builder.setInsertionPointToStart(file.getBody());
  auto seedPolicy = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
      builder.getNamedAttr("value", builder.getI64IntegerAttr(0)),
  });
  auto resultSchema = builder.getDictionaryAttr({
      builder.getNamedAttr("id", builder.getStringAttr("default")),
      builder.getNamedAttr("format", builder.getStringAttr("json")),
  });
  auto system = SystemOp::create(builder, loc, "soc", "Top", "root", 0, "cycle",
                                 mlir::FlatSymbolRefAttr(), seedPolicy,
                                 builder.getArrayAttr({}), resultSchema, true);
  EXPECT_TRUE(system);
  EXPECT_TRUE(mlir::isa<mlir::FunctionOpInterface>(top.getOperation()));
  EXPECT_TRUE(mlir::isa<mlir::RegionKindInterface>(top.getOperation()));
  EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
  EXPECT_TRUE(mlir::succeeded(verifyGraphStructure(file)));
}

TEST(ACIROpsTest, TaskSixRegistryDeltaIsExactlyNineGraphOperations) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  const std::array<llvm::StringLiteral, 9> names = {
      "ac.system",           "ac.module",   "ac.module.extern",
      "ac.module.generated", "ac.instance", "ac.array",
      "ac.instances",        "ac.view",     "ac.return",
  };
  for (llvm::StringLiteral name : names)
    EXPECT_TRUE(mlir::OperationName(name, &context).isRegistered())
        << name.str();
  EXPECT_FALSE(mlir::OperationName("ac.connect", &context).isRegistered());
  EXPECT_FALSE(mlir::OperationName("ac.queue", &context).isRegistered());
  EXPECT_FALSE(mlir::OperationName("ac.time_domain", &context).isRegistered());
}

TEST(ACIROpsTest, LargeArrayVerificationIsDeterministic) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  getStructuralProviderRegistry(&context).registerExternal("Leaf");
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto file = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(file.getBody());
  auto emptyType = builder.getFunctionType({}, {});
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto binding = builder.getDictionaryAttr({
      builder.getNamedAttr("registry", builder.getStringAttr("cpp")),
      builder.getNamedAttr("name", builder.getStringAttr("Leaf")),
  });
  ModuleExternOp::create(builder, loc, "Leaf", emptyType, mlir::StringAttr(),
                         emptyDictionary, binding);
  auto top = ModuleOp::create(builder, loc, "Top", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(top.addEntryBlock());
  constexpr int64_t elementCount = 4096;
  llvm::SmallVector<mlir::Attribute> arguments(elementCount, emptyDictionary);
  ArrayOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "Leaf",
                  "large", "large", "large", llvm::ArrayRef<int64_t>{64, 64},
                  builder.getArrayAttr(arguments));
  ReturnOp::create(builder, loc, mlir::ValueRange{});

  EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
  EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
  EXPECT_TRUE(mlir::succeeded(verifyGraphStructure(file)));
  EXPECT_EQ(buildArrayElementPath("root.large", {1, 2, 3}),
            "root.large[1][2][3]");
}

TEST(ACIROpsTest, ModulePortMetadataPrintsAndReparsesCanonically) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  constexpr llvm::StringLiteral source = R"mlir(
    ac.module @M(%x : i32 {ac.port_name = "input"})
        -> (i32 {ac.port_name = "output"}) parameters {}
        attributes {ac.graph_label = "graph"} graph {
      ac.return %x : i32
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  auto operation = *module->getOps<ModuleOp>().begin();
  ASSERT_TRUE(operation.getArgAttrsAttr());
  ASSERT_TRUE(operation.getResAttrsAttr());
  std::string printed;
  llvm::raw_string_ostream(printed) << *module;
  auto reparsed = mlir::parseSourceString<mlir::ModuleOp>(printed, &context);
  ASSERT_TRUE(reparsed);
  auto reparsedOperation = *reparsed->getOps<ModuleOp>().begin();
  EXPECT_EQ(operation.getArgAttrsAttr(), reparsedOperation.getArgAttrsAttr());
  EXPECT_EQ(operation.getResAttrsAttr(), reparsedOperation.getResAttrsAttr());
  EXPECT_EQ(operation->getAttr("ac.graph_label"),
            reparsedOperation->getAttr("ac.graph_label"));
}

TEST(ACIROpsTest, StructuralProvidersAreContextOwnedAndExact) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  auto &providers = getStructuralProviderRegistry(&context);
  providers.registerExternal("test.external");
  providers.registerGenerator("test.generator");
  EXPECT_TRUE(providers.hasExternal("test.external"));
  EXPECT_TRUE(providers.hasGenerator("test.generator"));
  EXPECT_FALSE(providers.hasExternal("test.generator"));
  EXPECT_FALSE(providers.hasGenerator("test.external"));
}

TEST(ACIROpsTest, StaticUnitDictionaryUsesClosedACIRUnitSet) {
  mlir::MLIRContext context;
  mlir::Builder builder(&context);
  auto unitValue = [&](llvm::StringRef unit) {
    return builder.getDictionaryAttr({
        builder.getNamedAttr("value", builder.getI64IntegerAttr(1)),
        builder.getNamedAttr("unit", builder.getStringAttr(unit)),
    });
  };
  for (llvm::StringRef unit :
       {"ticks", "cycles", "seconds", "milliseconds", "microseconds",
        "nanoseconds", "picoseconds", "bytes", "bits", "entries", "packets",
        "transactions"})
    EXPECT_TRUE(isConcreteStaticValue(unitValue(unit))) << unit.str();
  EXPECT_FALSE(isConcreteStaticValue(unitValue("bananas")));
  EXPECT_FALSE(isConcreteStaticValue(unitValue("")));
}

TEST(ACIROpsTest, HierarchyDepthAndOwnerBudgetsRejectCompactGraphs) {
  auto buildGraph = [](mlir::MLIRContext &context, unsigned moduleCount,
                       unsigned fanout, llvm::StringRef prefix) {
    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();
    auto file = mlir::ModuleOp::create(loc);
    auto emptyType = builder.getFunctionType({}, {});
    auto emptyDictionary = builder.getDictionaryAttr({});
    builder.setInsertionPointToStart(file.getBody());
    for (unsigned index = 0; index != moduleCount; ++index) {
      std::string name = (prefix + std::to_string(index)).str();
      auto module =
          ModuleOp::create(builder, loc, name, emptyType, emptyDictionary);
      builder.setInsertionPointToStart(module.addEntryBlock());
      if (index + 1 != moduleCount) {
        std::string target = (prefix + std::to_string(index + 1)).str();
        for (unsigned child = 0; child != fanout; ++child) {
          std::string segment = "child" + std::to_string(child);
          InstanceOp::create(builder, loc, mlir::TypeRange{},
                             mlir::ValueRange{}, target, segment, segment,
                             segment, emptyDictionary);
        }
      }
      ReturnOp::create(builder, loc, mlir::ValueRange{});
      builder.setInsertionPointToEnd(file.getBody());
    }
    auto seed = builder.getDictionaryAttr({
        builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
        builder.getNamedAttr("value", builder.getI64IntegerAttr(0)),
    });
    auto results = builder.getDictionaryAttr({
        builder.getNamedAttr("id", builder.getStringAttr("budget")),
        builder.getNamedAttr("format", builder.getStringAttr("json")),
    });
    std::string root = (prefix + "0").str();
    SystemOp::create(builder, loc, "budget", root, "root", 0, "cycle",
                     mlir::FlatSymbolRefAttr(), seed, builder.getArrayAttr({}),
                     results, true);
    return file;
  };

  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  llvm::SmallVector<std::string> diagnostics;
  mlir::ScopedDiagnosticHandler handler(
      &context, [&](mlir::Diagnostic &diagnostic) {
        std::string text;
        llvm::raw_string_ostream(text) << diagnostic;
        diagnostics.push_back(std::move(text));
        return mlir::success();
      });
  auto depthBoundary = buildGraph(context, 1025, 1, "Boundary");
  EXPECT_TRUE(mlir::succeeded(verifyGraphStructure(depthBoundary)));
  diagnostics.clear();
  auto tooDeep = buildGraph(context, 1026, 1, "Depth");
  EXPECT_TRUE(mlir::failed(verifyGraphStructure(tooDeep)));
  EXPECT_TRUE(llvm::any_of(diagnostics, [](llvm::StringRef diagnostic) {
    return diagnostic.contains("hierarchy depth exceeds bound 1024");
  }));
  diagnostics.clear();
  auto tooWide = buildGraph(context, 21, 2, "Wide");
  EXPECT_TRUE(mlir::failed(verifyGraphStructure(tooWide)));
  EXPECT_TRUE(llvm::any_of(diagnostics, [](llvm::StringRef diagnostic) {
    return diagnostic.contains("owner count exceeds bound 1048576");
  }));
  diagnostics.clear();
  auto veryDeep = buildGraph(context, 20001, 1, "VeryDeep");
  EXPECT_TRUE(mlir::failed(verifyGraphStructure(veryDeep)));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_TRUE(llvm::StringRef(diagnostics.front())
                  .contains("hierarchy depth exceeds bound 1024"));
}

TEST(ACIROpsTest, ExplicitViewProvenanceScalesNearLinearly) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto emptyType = builder.getFunctionType({}, {});
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto zeroShape = builder.getDenseI64ArrayAttr({0});

  auto buildChain = [&](unsigned viewCount, llvm::StringRef prefix) {
    auto file = mlir::ModuleOp::create(loc);
    builder.setInsertionPointToStart(file.getBody());
    auto leaf = ModuleOp::create(builder, loc, (prefix + "Leaf").str(),
                                 emptyType, emptyDictionary);
    builder.setInsertionPointToStart(leaf.addEntryBlock());
    ReturnOp::create(builder, loc, mlir::ValueRange{});
    builder.setInsertionPointToEnd(file.getBody());
    auto top = ModuleOp::create(builder, loc, (prefix + "Top").str(), emptyType,
                                emptyDictionary);
    builder.setInsertionPointToStart(top.addEntryBlock());
    std::string previous = "source";
    InstanceOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{},
                       (prefix + "Leaf").str(), previous, previous, previous,
                       emptyDictionary);
    for (unsigned index = 0; index != viewCount; ++index) {
      std::string name = "view" + std::to_string(index);
      ViewOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, name,
                     "permutation",
                     builder.getArrayAttr(
                         {mlir::FlatSymbolRefAttr::get(&context, previous)}),
                     builder.getArrayAttr({zeroShape}), mlir::IntegerAttr(),
                     llvm::ArrayRef<int64_t>{}, llvm::ArrayRef<int64_t>{0});
      previous = std::move(name);
    }
    ReturnOp::create(builder, loc, mlir::ValueRange{});
    return file;
  };

  auto buildWide = [&] {
    constexpr unsigned sourceCount = 1000;
    auto file = mlir::ModuleOp::create(loc);
    builder.setInsertionPointToStart(file.getBody());
    auto leaf =
        ModuleOp::create(builder, loc, "WideLeaf", emptyType, emptyDictionary);
    builder.setInsertionPointToStart(leaf.addEntryBlock());
    ReturnOp::create(builder, loc, mlir::ValueRange{});
    builder.setInsertionPointToEnd(file.getBody());
    auto top =
        ModuleOp::create(builder, loc, "WideTop", emptyType, emptyDictionary);
    builder.setInsertionPointToStart(top.addEntryBlock());
    llvm::SmallVector<mlir::Attribute> producerRefs;
    llvm::SmallVector<mlir::Attribute> sourceShapes;
    producerRefs.reserve(sourceCount);
    sourceShapes.reserve(sourceCount);
    for (unsigned index = 0; index != sourceCount; ++index) {
      std::string name = "source" + std::to_string(index);
      InstanceOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{},
                         "WideLeaf", name, name, name, emptyDictionary);
      producerRefs.push_back(mlir::FlatSymbolRefAttr::get(&context, name));
      sourceShapes.push_back(zeroShape);
    }
    ViewOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "wide",
                   "concat", builder.getArrayAttr(producerRefs),
                   builder.getArrayAttr(sourceShapes),
                   builder.getI64IntegerAttr(0), llvm::ArrayRef<int64_t>{},
                   llvm::ArrayRef<int64_t>{0});
    ReturnOp::create(builder, loc, mlir::ValueRange{});
    return file;
  };

  auto wide = buildWide();
  auto small = buildChain(1000, "Small");
  auto large = buildChain(5000, "Large");
  auto verifyTimed = [](mlir::ModuleOp file) {
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
    return std::chrono::steady_clock::now() - start;
  };
  auto wideElapsed = verifyTimed(wide);
  auto smallElapsed = verifyTimed(small);
  auto largeElapsed = verifyTimed(large);
  EXPECT_LT(wideElapsed, std::chrono::seconds(5));
  EXPECT_LT(largeElapsed, std::chrono::seconds(5));
  EXPECT_LT(largeElapsed, smallElapsed * 8 + std::chrono::milliseconds(50));
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
