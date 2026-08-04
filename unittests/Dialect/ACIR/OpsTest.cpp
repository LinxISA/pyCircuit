#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/ACIRResources.h"
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
#include <limits>

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
      "ac.address_map",
      "ac.address_space",
      "ac.enum",
      "ac.event",
      "ac.event_queue",
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
      "ac.queue",
      "ac.record.create",
      "ac.record.get",
      "ac.record.with",
      "ac.return",
      "ac.resource",
      "ac.role",
      "ac.state",
      "ac.struct",
      "ac.system",
      "ac.time_domain",
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
  EXPECT_FALSE(mlir::OperationName("ac.process", &context).isRegistered());
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

TEST(ACIROpsTest, NestedArraysCountTaskSevenOwnersBeforeElaboration) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto emptyType = builder.getFunctionType({}, {});
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto file = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(file.getBody());

  auto leaf =
      ModuleOp::create(builder, loc, "Leaf", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(leaf.addEntryBlock());
  auto latency = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
      builder.getNamedAttr("ticks", builder.getI64IntegerAttr(1)),
  });
  auto lifecycle = builder.getDictionaryAttr({
      builder.getNamedAttr("reservation",
                           builder.getStringAttr("propose_commit")),
      builder.getNamedAttr("release", builder.getStringAttr("balanced")),
      builder.getNamedAttr("cancellation", builder.getStringAttr("explicit")),
  });
  QueueOp::create(
      builder, loc, builder.getStringAttr("queue"),
      builder.getStringAttr("queue"), builder.getStringAttr("queue"),
      mlir::TypeAttr::get(builder.getI32Type()), builder.getI64IntegerAttr(1),
      mlir::IntegerAttr(), builder.getStringAttr("fifo"),
      mlir::FlatSymbolRefAttr::get(&context, "p"),
      builder.getStringAttr("exclusive"), mlir::DictionaryAttr(),
      builder.getI64IntegerAttr(1));
  EventQueueOp::create(
      builder, loc, builder.getStringAttr("events"),
      builder.getStringAttr("events"), builder.getStringAttr("events"),
      mlir::TypeAttr::get(EventType::get(&context, builder.getI32Type())),
      builder.getI64IntegerAttr(1), builder.getStringAttr("time_then_sequence"),
      mlir::FlatSymbolRefAttr::get(&context, "clock"),
      builder.getI64IntegerAttr(1));
  ResourceOp::create(builder, loc, "state", "state", "state", 1, 1, 1, latency,
                     lifecycle, "exclusive", mlir::FlatSymbolRefAttr(),
                     builder.getArrayAttr({}), 1);
  ReturnOp::create(builder, loc, mlir::ValueRange{});

  llvm::SmallVector<mlir::Attribute> staticArgs(
      512, mlir::Attribute(emptyDictionary));
  builder.setInsertionPointToEnd(file.getBody());
  auto middle =
      ModuleOp::create(builder, loc, "Middle", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(middle.addEntryBlock());
  ArrayOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "Leaf",
                  "leaves", "leaves", "leaves",
                  builder.getDenseI64ArrayAttr({512}),
                  builder.getArrayAttr(staticArgs));
  ReturnOp::create(builder, loc, mlir::ValueRange{});

  builder.setInsertionPointToEnd(file.getBody());
  auto top = ModuleOp::create(builder, loc, "Top", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(top.addEntryBlock());
  ArrayOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{}, "Middle",
                  "middles", "middles", "middles",
                  builder.getDenseI64ArrayAttr({512}),
                  builder.getArrayAttr(staticArgs));
  ReturnOp::create(builder, loc, mlir::ValueRange{});

  builder.setInsertionPointToEnd(file.getBody());
  auto seed = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
      builder.getNamedAttr("value", builder.getI64IntegerAttr(0)),
  });
  auto results = builder.getDictionaryAttr({
      builder.getNamedAttr("id", builder.getStringAttr("owners")),
      builder.getNamedAttr("format", builder.getStringAttr("json")),
  });
  SystemOp::create(builder, loc, "owners", "Top", "root", 0, "cycle",
                   mlir::FlatSymbolRefAttr(), seed, builder.getArrayAttr({}),
                   results, true);

  auto structureOnly = mlir::cast<mlir::ModuleOp>(file->clone());
  auto structureLeaf = *structureOnly.getOps<ModuleOp>().begin();
  for (mlir::Operation &operation :
       llvm::make_early_inc_range(structureLeaf.getBody().front()))
    if (mlir::isa<QueueOp, EventQueueOp, ResourceOp>(operation))
      operation.erase();
  EXPECT_TRUE(mlir::succeeded(verifyGraphStructure(structureOnly)));

  std::string diagnostic;
  mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic &value) {
    llvm::raw_string_ostream(diagnostic) << value;
    return mlir::success();
  });
  auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE(mlir::failed(verifyGraphStructure(file)));
  EXPECT_NE(diagnostic.find("owner count exceeds bound 1048576"),
            std::string::npos);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
}

TEST(ACIROpsTest, TaskSevenOwnersRegisterAtDistinctAbsoluteInstancePaths) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  auto emptyType = builder.getFunctionType({}, {});
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto file = mlir::ModuleOp::create(loc);
  builder.setInsertionPointToStart(file.getBody());
  auto leaf =
      ModuleOp::create(builder, loc, "Leaf", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(leaf.addEntryBlock());
  auto latency = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
      builder.getNamedAttr("ticks", builder.getI64IntegerAttr(1)),
  });
  auto lifecycle = builder.getDictionaryAttr({
      builder.getNamedAttr("reservation",
                           builder.getStringAttr("propose_commit")),
      builder.getNamedAttr("release", builder.getStringAttr("balanced")),
      builder.getNamedAttr("cancellation", builder.getStringAttr("explicit")),
  });
  QueueOp::create(
      builder, loc, builder.getStringAttr("queue"),
      builder.getStringAttr("queue"), builder.getStringAttr("queue"),
      mlir::TypeAttr::get(builder.getI32Type()), builder.getI64IntegerAttr(1),
      mlir::IntegerAttr(), builder.getStringAttr("fifo"),
      mlir::FlatSymbolRefAttr::get(&context, "p"),
      builder.getStringAttr("exclusive"), mlir::DictionaryAttr(),
      builder.getI64IntegerAttr(1));
  EventQueueOp::create(
      builder, loc, builder.getStringAttr("events"),
      builder.getStringAttr("events"), builder.getStringAttr("events"),
      mlir::TypeAttr::get(EventType::get(&context, builder.getI32Type())),
      builder.getI64IntegerAttr(1), builder.getStringAttr("time_then_sequence"),
      mlir::FlatSymbolRefAttr::get(&context, "clock"),
      builder.getI64IntegerAttr(1));
  ResourceOp::create(builder, loc, "state", "state", "state", 1, 1, 1, latency,
                     lifecycle, "exclusive", mlir::FlatSymbolRefAttr(),
                     builder.getArrayAttr({}), 1);
  ReturnOp::create(builder, loc, mlir::ValueRange{});
  builder.setInsertionPointToEnd(file.getBody());
  auto top = ModuleOp::create(builder, loc, "Top", emptyType, emptyDictionary);
  builder.setInsertionPointToStart(top.addEntryBlock());
  InstanceOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{},
                     "Leaf", "left", "left", "left", emptyDictionary);
  InstanceOp::create(builder, loc, mlir::TypeRange{}, mlir::ValueRange{},
                     "Leaf", "right", "right", "right", emptyDictionary);
  ReturnOp::create(builder, loc, mlir::ValueRange{});
  builder.setInsertionPointToEnd(file.getBody());
  auto seed = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", builder.getStringAttr("fixed")),
      builder.getNamedAttr("value", builder.getI64IntegerAttr(0)),
  });
  auto results = builder.getDictionaryAttr({
      builder.getNamedAttr("id", builder.getStringAttr("owners")),
      builder.getNamedAttr("format", builder.getStringAttr("json")),
  });
  SystemOp::create(builder, loc, "owners", "Top", "root", 0, "cycle",
                   mlir::FlatSymbolRefAttr(), seed, builder.getArrayAttr({}),
                   results, true);
  llvm::SmallVector<ElaboratedStateOwner> owners;
  ASSERT_TRUE(mlir::succeeded(collectElaboratedStateOwners(file, owners)));
  ASSERT_EQ(owners.size(), 6u);
  EXPECT_EQ(owners[0].path, "root.left.queue");
  EXPECT_EQ(owners[0].stableId, "root/left/queue");
  EXPECT_EQ(owners[1].path, "root.left.events");
  EXPECT_EQ(owners[1].stableId, "root/left/events");
  EXPECT_EQ(owners[2].path, "root.left.state");
  EXPECT_EQ(owners[2].stableId, "root/left/state");
  EXPECT_EQ(owners[3].path, "root.right.queue");
  EXPECT_EQ(owners[3].stableId, "root/right/queue");
  EXPECT_EQ(owners[4].path, "root.right.events");
  EXPECT_EQ(owners[4].stableId, "root/right/events");
  EXPECT_EQ(owners[5].path, "root.right.state");
  EXPECT_EQ(owners[5].stableId, "root/right/state");
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

TEST(ACIRResourcesTest, CheckedArithmeticAndIntervalsRejectBoundaries) {
  uint64_t result = 0;
  EXPECT_TRUE(checkedAdd(4, 5, result));
  EXPECT_EQ(result, 9u);
  EXPECT_FALSE(checkedAdd(std::numeric_limits<uint64_t>::max(), 1, result));
  EXPECT_TRUE(checkedMultiply(7, 6, result));
  EXPECT_EQ(result, 42u);
  EXPECT_FALSE(
      checkedMultiply(std::numeric_limits<uint64_t>::max(), 2, result));
  EXPECT_TRUE(intervalsOverlap({0, 8}, {7, 9}));
  EXPECT_FALSE(intervalsOverlap({0, 8}, {8, 9}));
  EXPECT_TRUE(intervalsOverlap(
      {uint64_t{1} << 63, WideAddress{1} << 64},
      {std::numeric_limits<uint64_t>::max(), WideAddress{1} << 64}));
  EXPECT_EQ(compareAddressMapOrder({0, 8, true, 2}, {0, 4, true, 1}), -1);
}

TEST(ACIRResourcesTest, RationalNormalizationIsExactAndBounded) {
  uint64_t ticks = 0;
  EXPECT_TRUE(normalizeRationalToTicks(3, 2, 1, 2, ticks));
  EXPECT_EQ(ticks, 3u);
  EXPECT_FALSE(normalizeRationalToTicks(1, 3, 1, 2, ticks));
  EXPECT_FALSE(normalizeRationalToTicks(1, 0, 1, 1, ticks));
  EXPECT_FALSE(normalizeRationalToTicks(std::numeric_limits<uint64_t>::max(), 1,
                                        1, 2, ticks));
  EXPECT_TRUE(normalizeRationalToTicks(0, 1, 1, 1, ticks));
  EXPECT_EQ(ticks, 0u);
  EXPECT_TRUE(normalizeRationalToTicks(std::numeric_limits<int64_t>::max(), 1,
                                       1, 1, ticks));
  EXPECT_EQ(ticks, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  EXPECT_FALSE(normalizeRationalToTicks(
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1, 1, 1, 1,
      ticks));
  EXPECT_TRUE(normalizeRationalToTicks(1, 1, 1, kMaxTickScale, ticks));
  EXPECT_EQ(ticks, kMaxTickScale);
  EXPECT_FALSE(normalizeRationalToTicks(1, 1, 1, kMaxTickScale + 1, ticks));
  EXPECT_FALSE(normalizeRationalToTicks(1, kMaxTickScale + 1, 1,
                                        kMaxTickScale + 1, ticks));
  EXPECT_TRUE(normalizeRationalToTicks(3, 2, 3, 4, ticks));
  EXPECT_EQ(ticks, 2u);
  EXPECT_FALSE(normalizeRationalToTicks(std::numeric_limits<uint64_t>::max(), 1,
                                        1, 2, ticks));
}

TEST(ACIRResourcesTest, DomainTickUsesNormativePhasePlusCycleTimesPeriod) {
  uint64_t tick = 0;
  EXPECT_TRUE(
      checkedDomainTick(std::numeric_limits<int64_t>::max(), 1, 0, tick));
  EXPECT_EQ(tick, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  EXPECT_FALSE(
      checkedDomainTick(std::numeric_limits<int64_t>::max(), 1, 1, tick));
}

TEST(ACIRResourcesTest, TaskSevenRegistryDeltaIsExactlySixOperations) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  const std::array<llvm::StringLiteral, 6> names = {
      "ac.queue",         "ac.event_queue", "ac.resource",
      "ac.address_space", "ac.address_map", "ac.time_domain",
  };
  for (llvm::StringLiteral name : names)
    EXPECT_TRUE(mlir::OperationName(name, &context).isRegistered())
        << name.str();
  EXPECT_FALSE(mlir::OperationName("ac.process", &context).isRegistered());
  EXPECT_FALSE(mlir::OperationName("ac.try_issue", &context).isRegistered());
}

TEST(ACIRResourcesTest, PublicBuildersAndTypedEffectsCoverAllSixOperations) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  auto file = mlir::ModuleOp::create(location);
  builder.setInsertionPointToStart(file.getBody());
  auto module =
      ModuleOp::create(builder, location, "M", builder.getFunctionType({}, {}),
                       builder.getDictionaryAttr({}));
  builder.setInsertionPointToStart(module.addEntryBlock());

  auto i64 = [&](int64_t value) { return builder.getI64IntegerAttr(value); };
  auto string = [&](llvm::StringRef value) {
    return builder.getStringAttr(value);
  };
  auto symbol = [&](llvm::StringRef value) {
    return mlir::FlatSymbolRefAttr::get(&context, value);
  };
  auto queue =
      QueueOp::create(builder, location, string("q"), string("q"), string("q"),
                      mlir::TypeAttr::get(builder.getI32Type()), i64(8),
                      mlir::IntegerAttr(), string("fifo"), symbol("p"),
                      string("exclusive"), mlir::DictionaryAttr(), i64(1));
  auto domain = TimeDomainOp::create(builder, location, string("clock"), i64(1),
                                     i64(0), i64(1), mlir::FlatSymbolRefAttr(),
                                     mlir::DictionaryAttr());
  auto eventQueue = EventQueueOp::create(
      builder, location, string("events"), string("events"), string("events"),
      mlir::TypeAttr::get(EventType::get(&context, builder.getI32Type())),
      i64(8), string("time_then_sequence"), symbol("clock"), i64(1));
  auto latency = builder.getDictionaryAttr({
      builder.getNamedAttr("kind", string("fixed")),
      builder.getNamedAttr("ticks", i64(2)),
  });
  auto lifecycle = builder.getDictionaryAttr({
      builder.getNamedAttr("reservation", string("propose_commit")),
      builder.getNamedAttr("release", string("balanced")),
      builder.getNamedAttr("cancellation", string("explicit")),
  });
  auto resource = ResourceOp::create(
      builder, location, string("r"), string("r"), string("r"), i64(2), i64(1),
      i64(1), latency, lifecycle, string("exclusive"),
      mlir::FlatSymbolRefAttr(), builder.getArrayAttr({}), i64(1));
  auto address = AddressSpaceOp::create(
      builder, location, string("memory"), string("memory"), string("memory"),
      i64(32), string("byte"), mlir::Attribute(), mlir::FlatSymbolRefAttr(),
      mlir::DictionaryAttr());
  auto addressMap =
      AddressMapOp::create(builder, location, string("map"), symbol("memory"),
                           builder.getArrayAttr({}),
                           builder.getDictionaryAttr({
                               builder.getNamedAttr("kind", string("unmapped")),
                           }));
  ReturnOp::create(builder, location, mlir::ValueRange{});

  EXPECT_TRUE(queue && eventQueue && resource && address && addressMap &&
              domain);
  auto hasWriteOn = [](mlir::Operation *operation,
                       mlir::SideEffects::Resource *resourceKind) {
    llvm::SmallVector<mlir::MemoryEffects::EffectInstance> effects;
    mlir::cast<mlir::MemoryEffectOpInterface>(operation).getEffects(effects);
    return llvm::any_of(effects, [&](const auto &effect) {
      return mlir::isa<mlir::MemoryEffects::Write>(effect.getEffect()) &&
             effect.getResource() == resourceKind;
    });
  };
  EXPECT_TRUE(hasWriteOn(queue, QueueStateResource::get()));
  EXPECT_TRUE(hasWriteOn(eventQueue, EventQueueStateResource::get()));
  EXPECT_TRUE(hasWriteOn(resource, ReservationStateResource::get()));
  EXPECT_FALSE(mlir::isMemoryEffectFree(queue));
  EXPECT_FALSE(mlir::isMemoryEffectFree(eventQueue));
  EXPECT_FALSE(mlir::isMemoryEffectFree(resource));

  auto effectOf = [](mlir::Operation *operation) {
    llvm::SmallVector<mlir::MemoryEffects::EffectInstance> effects;
    mlir::cast<mlir::MemoryEffectOpInterface>(operation).getEffects(effects);
    EXPECT_EQ(effects.size(), 1u);
    return effects.front();
  };
  auto queueEffect = effectOf(queue);
  auto eventEffect = effectOf(eventQueue);
  auto queueEffectAgain = effectOf(queue);
  auto qualified = [&](llvm::StringRef local) {
    return mlir::SymbolRefAttr::get(
        &context, "M", {mlir::FlatSymbolRefAttr::get(&context, local)});
  };
  EXPECT_EQ(queueEffect.getSymbolRef(), qualified("q"));
  EXPECT_EQ(eventEffect.getSymbolRef(), qualified("events"));
  EXPECT_EQ(queueEffect.getSymbolRef(), queueEffectAgain.getSymbolRef());
  EXPECT_EQ(queueEffect.getParameters(), queueEffectAgain.getParameters());
  EXPECT_NE(queueEffect.getSymbolRef(), eventEffect.getSymbolRef());
  auto parameters =
      mlir::cast<mlir::DictionaryAttr>(queueEffect.getParameters());
  EXPECT_EQ(parameters.getAs<mlir::StringAttr>("stable_id").getValue(), "q");
  EXPECT_EQ(parameters.getAs<mlir::StringAttr>("path").getValue(), "q");
}

TEST(ACIRResourcesTest, EffectsUseDefinitionQualifiedPreFreezeIdentity) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  auto file = mlir::ModuleOp::create(location);
  auto buildQueue = [&](llvm::StringRef definition) {
    builder.setInsertionPointToEnd(file.getBody());
    auto module = ModuleOp::create(builder, location, definition,
                                   builder.getFunctionType({}, {}),
                                   builder.getDictionaryAttr({}));
    builder.setInsertionPointToStart(module.addEntryBlock());
    auto queue = QueueOp::create(
        builder, location, builder.getStringAttr("q"),
        builder.getStringAttr("q"), builder.getStringAttr("q"),
        mlir::TypeAttr::get(builder.getI32Type()), builder.getI64IntegerAttr(1),
        mlir::IntegerAttr(), builder.getStringAttr("fifo"),
        mlir::FlatSymbolRefAttr::get(&context, "p"),
        builder.getStringAttr("exclusive"), mlir::DictionaryAttr(),
        builder.getI64IntegerAttr(1));
    ReturnOp::create(builder, location, mlir::ValueRange{});
    return queue;
  };
  QueueOp left = buildQueue("Left");
  QueueOp right = buildQueue("Right");
  auto effectOf = [](QueueOp queue) {
    llvm::SmallVector<mlir::MemoryEffects::EffectInstance> effects;
    mlir::cast<mlir::MemoryEffectOpInterface>(*queue).getEffects(effects);
    EXPECT_EQ(effects.size(), 1u);
    return effects.front();
  };
  auto leftEffect = effectOf(left);
  auto leftAgain = effectOf(left);
  auto rightEffect = effectOf(right);
  auto qualified = [&](llvm::StringRef definition) {
    return mlir::SymbolRefAttr::get(
        &context, definition, {mlir::FlatSymbolRefAttr::get(&context, "q")});
  };
  EXPECT_EQ(leftEffect.getSymbolRef(), qualified("Left"));
  EXPECT_EQ(rightEffect.getSymbolRef(), qualified("Right"));
  EXPECT_NE(leftEffect.getSymbolRef(), rightEffect.getSymbolRef());
  EXPECT_EQ(leftEffect.getSymbolRef(), leftAgain.getSymbolRef());
  EXPECT_EQ(leftEffect.getParameters(), leftAgain.getParameters());
  auto parameters =
      mlir::cast<mlir::DictionaryAttr>(leftEffect.getParameters());
  auto identityPhase = parameters.getAs<mlir::StringAttr>("identity_phase");
  ASSERT_TRUE(identityPhase);
  EXPECT_EQ(identityPhase.getValue(), "definition_pre_freeze");
}

TEST(ACIRResourcesTest, LargeAddressMapAndParentGraphScaleNearLinearly) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  auto emptyDictionary = builder.getDictionaryAttr({});
  auto file = mlir::ModuleOp::create(location);
  builder.setInsertionPointToStart(file.getBody());
  auto module = ModuleOp::create(
      builder, location, "M", builder.getFunctionType({}, {}), emptyDictionary);
  builder.setInsertionPointToStart(module.addEntryBlock());
  AddressSpaceOp::create(
      builder, location, builder.getStringAttr("space"),
      builder.getStringAttr("space"), builder.getStringAttr("space"),
      builder.getI64IntegerAttr(32), builder.getStringAttr("byte"),
      mlir::Attribute(), mlir::FlatSymbolRefAttr(), mlir::DictionaryAttr());

  constexpr unsigned entryCount = 10000;
  llvm::SmallVector<mlir::Attribute> entries;
  entries.reserve(entryCount);
  for (unsigned index = 0; index != entryCount; ++index) {
    entries.push_back(builder.getDictionaryAttr({
        builder.getNamedAttr("base", builder.getI64IntegerAttr(0)),
        builder.getNamedAttr("size", builder.getI64IntegerAttr(entryCount)),
        builder.getNamedAttr("target",
                             mlir::FlatSymbolRefAttr::get(&context, "space")),
        builder.getNamedAttr("offset", builder.getI64IntegerAttr(0)),
        builder.getNamedAttr(
            "permissions",
            builder.getArrayAttr({builder.getStringAttr("read")})),
        builder.getNamedAttr("classes", builder.getArrayAttr({})),
        builder.getNamedAttr(
            "interleave",
            builder.getDictionaryAttr({
                builder.getNamedAttr("granularity",
                                     builder.getI64IntegerAttr(1)),
                builder.getNamedAttr("banks",
                                     builder.getI64IntegerAttr(entryCount)),
                builder.getNamedAttr("bank", builder.getI64IntegerAttr(index)),
            })),
    }));
  }
  AddressMapOp::create(
      builder, location, "map", "space", builder.getArrayAttr(entries),
      builder.getDictionaryAttr(
          {builder.getNamedAttr("kind", builder.getStringAttr("unmapped"))}));
  ReturnOp::create(builder, location, mlir::ValueRange{});

  auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
  auto verifyElapsed = std::chrono::steady_clock::now() - start;

  constexpr unsigned domainCount = 10000;
  auto graphFile = mlir::ModuleOp::create(location);
  builder.setInsertionPointToStart(graphFile.getBody());
  auto bridgeModule =
      ModuleOp::create(builder, location, "Bridge",
                       builder.getFunctionType({}, {}), emptyDictionary);
  builder.setInsertionPointToStart(bridgeModule.addEntryBlock());
  ReturnOp::create(builder, location, mlir::ValueRange{});
  builder.setInsertionPointToEnd(graphFile.getBody());
  auto graphModule =
      ModuleOp::create(builder, location, "Graph",
                       builder.getFunctionType({}, {}), emptyDictionary);
  builder.setInsertionPointToStart(graphModule.addEntryBlock());
  InstanceOp::create(builder, location, mlir::TypeRange{}, mlir::ValueRange{},
                     "Bridge", "bridge", "bridge", "bridge", emptyDictionary);
  for (unsigned index = 0; index != domainCount; ++index) {
    std::string name = "d" + std::to_string(index);
    mlir::FlatSymbolRefAttr parent;
    mlir::DictionaryAttr bridge;
    if (index) {
      parent = mlir::FlatSymbolRefAttr::get(&context,
                                            "d" + std::to_string(index - 1));
      bridge = builder.getDictionaryAttr({
          builder.getNamedAttr("kind", builder.getStringAttr("explicit")),
          builder.getNamedAttr(
              "owner", mlir::FlatSymbolRefAttr::get(&context, "bridge")),
      });
    }
    TimeDomainOp::create(builder, location, builder.getStringAttr(name),
                         builder.getI64IntegerAttr(1),
                         builder.getI64IntegerAttr(0),
                         builder.getI64IntegerAttr(1), parent, bridge);
  }
  ReturnOp::create(builder, location, mlir::ValueRange{});

  start = std::chrono::steady_clock::now();
  EXPECT_TRUE(mlir::succeeded(mlir::verify(graphFile)));
  auto graphElapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(verifyElapsed, std::chrono::seconds(5));
  EXPECT_LT(graphElapsed, std::chrono::seconds(5));
}

TEST(ACIRResourcesTest, MixedGeometryDistinctPrioritiesScaleNearLinearly) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  mlir::OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  auto buildMap = [&](unsigned entryCount) {
    auto file = mlir::ModuleOp::create(location);
    builder.setInsertionPointToStart(file.getBody());
    auto module = ModuleOp::create(builder, location, "M",
                                   builder.getFunctionType({}, {}),
                                   builder.getDictionaryAttr({}));
    builder.setInsertionPointToStart(module.addEntryBlock());
    AddressSpaceOp::create(
        builder, location, builder.getStringAttr("space"),
        builder.getStringAttr("space"), builder.getStringAttr("space"),
        builder.getI64IntegerAttr(32), builder.getStringAttr("byte"),
        mlir::Attribute(), mlir::FlatSymbolRefAttr(), mlir::DictionaryAttr());
    llvm::SmallVector<mlir::Attribute> entries;
    entries.reserve(entryCount);
    for (uint64_t priority = 1; priority <= entryCount; ++priority) {
      entries.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("base", builder.getI64IntegerAttr(0)),
          builder.getNamedAttr("size", builder.getI64IntegerAttr(priority)),
          builder.getNamedAttr("target",
                               mlir::FlatSymbolRefAttr::get(&context, "space")),
          builder.getNamedAttr("offset", builder.getI64IntegerAttr(0)),
          builder.getNamedAttr(
              "permissions",
              builder.getArrayAttr({builder.getStringAttr("read")})),
          builder.getNamedAttr("classes", builder.getArrayAttr({})),
          builder.getNamedAttr("priority", builder.getI64IntegerAttr(priority)),
          builder.getNamedAttr(
              "interleave",
              builder.getDictionaryAttr({
                  builder.getNamedAttr("granularity",
                                       builder.getI64IntegerAttr(1)),
                  builder.getNamedAttr("banks",
                                       builder.getI64IntegerAttr(priority)),
                  builder.getNamedAttr("bank", builder.getI64IntegerAttr(0)),
              })),
      }));
    }
    AddressMapOp::create(
        builder, location, "map", "space", builder.getArrayAttr(entries),
        builder.getDictionaryAttr(
            {builder.getNamedAttr("kind", builder.getStringAttr("unmapped"))}));
    ReturnOp::create(builder, location, mlir::ValueRange{});
    return file;
  };
  auto verifyTimed = [&](unsigned entryCount) {
    mlir::ModuleOp file = buildMap(entryCount);
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(mlir::succeeded(mlir::verify(file)));
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
  };

  int64_t fiveThousandMs = verifyTimed(5000);
  int64_t tenThousandMs = verifyTimed(10000);
  RecordProperty("five_thousand_ms", fiveThousandMs);
  RecordProperty("ten_thousand_ms", tenThousandMs);
  EXPECT_LT(tenThousandMs, 5000);
  EXPECT_LT(tenThousandMs, std::max<int64_t>(100, fiveThousandMs * 3));
}

} // namespace
} // namespace acir::ac
