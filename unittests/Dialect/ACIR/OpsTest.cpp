#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/ACIROps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "gtest/gtest.h"

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
  auto types = builder.getTypeArrayAttr({builder.getI8Type()});
  auto serialization = builder.getDictionaryAttr({
      builder.getNamedAttr("size", builder.getI64IntegerAttr(1)),
      builder.getNamedAttr("alignment", builder.getI64IntegerAttr(1)),
      builder.getNamedAttr("endianness", builder.getStringAttr("little")),
  });

  EXPECT_TRUE(TypeAliasOp::create(builder, loc, "Byte", builder.getI8Type()));
  EXPECT_TRUE(StructOp::create(builder, loc, "S", names, types));
  EXPECT_TRUE(
      EnumOp::create(builder, loc, "E", builder.getStrArrayAttr({"a"})));
  EXPECT_TRUE(UnionOp::create(builder, loc, "U", names, types, "x"));
  EXPECT_TRUE(PacketOp::create(builder, loc, "P", names, types, serialization));
  EXPECT_TRUE(TransactionOp::create(builder, loc, "T", names, types));

  auto input = mlir::UnrealizedConversionCastOp::create(
                   builder, loc, mlir::TypeRange{builder.getI8Type()},
                   mlir::ValueRange{})
                   .getResult(0);
  auto structType =
      StructType::get(&context, mlir::FlatSymbolRefAttr::get(&context, "S"));
  auto packetType =
      PacketType::get(&context, mlir::FlatSymbolRefAttr::get(&context, "P"));
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
  auto bytes = PacketSerializeOp::create(builder, loc, bytesType, packet, "P");
  EXPECT_TRUE(bytes);
  auto deserialize = PacketDeserializeOp::create(builder, loc, packetType,
                                                 bytes.getResult(), "P");
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
  auto structType =
      StructType::get(&context, mlir::FlatSymbolRefAttr::get(&context, "S"));
  auto record = RecordCreateOp::create(builder, loc, structType,
                                       mlir::ValueRange{input.getResult(0)},
                                       builder.getStrArrayAttr({"x"}));
  auto packetType =
      PacketType::get(&context, mlir::FlatSymbolRefAttr::get(&context, "P"));
  auto packet = mlir::UnrealizedConversionCastOp::create(
      builder, loc, mlir::TypeRange{packetType}, mlir::ValueRange{});
  auto serialized = PacketSerializeOp::create(
      builder, loc, VectorType::get(&context, 1, builder.getI8Type()),
      packet.getResult(0), "P");
  EXPECT_TRUE(mlir::isMemoryEffectFree(record.getOperation()));
  EXPECT_TRUE(mlir::isMemoryEffectFree(serialized.getOperation()));
}

TEST(ACIROpsTest, ClosedRegistryDoesNotLoadUnrelatedDialects) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  EXPECT_NE(context.getLoadedDialect("ac"), nullptr);
  EXPECT_EQ(context.getLoadedDialect("dlti"), nullptr);
  EXPECT_EQ(context.getLoadedDialect("arith"), nullptr);
  EXPECT_EQ(context.getLoadedDialect("scf"), nullptr);
}

} // namespace
} // namespace acir::ac
