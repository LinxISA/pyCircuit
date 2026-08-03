#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/GraphRegion.h"
#include "acir/InitAllDialects.h"
#include "acir/InitAllPasses.h"
#include "mlir/AsmParser/AsmParser.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>

namespace {

enum class CanonicalScanResult { Canonical, GenericOperation, MalformedEscape };

CanonicalScanResult scanCanonicalAssembly(llvm::StringRef input) {
  mlir::MLIRContext stringContext;
  mlir::ScopedDiagnosticHandler suppressStringDiagnostics(
      &stringContext, [](mlir::Diagnostic &) { return mlir::success(); });
  for (size_t index = 0; index < input.size();) {
    if (input.substr(index).starts_with("//")) {
      index = input.find('\n', index);
      if (index == llvm::StringRef::npos)
        return CanonicalScanResult::Canonical;
      continue;
    }
    if (input.substr(index).starts_with("/*")) {
      unsigned depth = 1;
      index += 2;
      while (index < input.size() && depth) {
        if (input.substr(index).starts_with("/*")) {
          ++depth;
          index += 2;
        } else if (input.substr(index).starts_with("*/")) {
          --depth;
          index += 2;
        } else {
          ++index;
        }
      }
      continue;
    }
    if (input[index] != '"') {
      ++index;
      continue;
    }
    size_t start = index++;
    bool escaped = false;
    while (index < input.size()) {
      char value = input[index++];
      if (!escaped && value == '"')
        break;
      escaped = !escaped && value == '\\';
      if (value != '\\')
        escaped = false;
    }
    llvm::StringRef token = input.slice(start, index);
    size_t next = index;
    while (next < input.size() && llvm::isSpace(input[next]))
      ++next;
    if (next >= input.size() || input[next] != '(')
      continue;
    auto parsed = mlir::parseAttribute(token, &stringContext);
    auto spelling = mlir::dyn_cast_or_null<mlir::StringAttr>(parsed);
    if (!spelling)
      return CanonicalScanResult::MalformedEscape;
    llvm::StringRef value = spelling.getValue();
    if (value.starts_with("ac.") || value.starts_with("acsim."))
      return CanonicalScanResult::GenericOperation;
  }
  return CanonicalScanResult::Canonical;
}

} // namespace

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  acir::registerAllDialects(registry);
#ifdef ACIR_INTERNAL_TEST_TOOL
  registry.addExtension(
      +[](mlir::MLIRContext *context, acir::ac::ACIRDialect *) {
        auto &providers = acir::ac::getStructuralProviderRegistry(context);
        for (llvm::StringRef name : {"A", "B", "Empty", "Ext", "Leaf", "Top"})
          providers.registerExternal(name);
        providers.registerGenerator("Gen");
      });
#endif
  acir::registerAllPasses();

  auto [inputFilename, outputFilename] = mlir::registerAndParseCLIOptions(
      argc, argv, "Agentic Circuit optimizer driver\n", registry);
  mlir::MlirOptMainConfig config =
      mlir::MlirOptMainConfig::createFromCLOptions();
  mlir::MlirOptMainConfig commandLineConfig = config;
  config.allowUnregisteredDialects(false)
      .useExplicitModule(true)
      .setPassPipelineSetupFn(
          [commandLineConfig](mlir::PassManager &passManager) {
            passManager.addPass(std::make_unique<acir::VerifyACIRFilePass>());
            return commandLineConfig.setupPassPipeline(passManager);
          });

  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> input =
      mlir::openInputFile(inputFilename, &errorMessage);
  if (!input) {
    llvm::errs() << errorMessage << '\n';
    return EXIT_FAILURE;
  }

#ifndef ACIR_INTERNAL_TEST_TOOL
  llvm::StringRef contents = input->getBuffer();
  bool isBytecode = contents.size() >= 4 &&
                    contents.take_front(4) == llvm::StringRef("ML\xefR", 4);
  if (!isBytecode) {
    switch (scanCanonicalAssembly(contents)) {
    case CanonicalScanResult::Canonical:
      break;
    case CanonicalScanResult::GenericOperation:
      llvm::errs()
          << "error: generic ACIR operation spelling is internal-only; "
             "use canonical ACIR assembly\n";
      return EXIT_FAILURE;
    case CanonicalScanResult::MalformedEscape:
      llvm::errs() << "error: malformed quoted operation name escape\n";
      return EXIT_FAILURE;
    }
  }
#endif

  std::unique_ptr<llvm::ToolOutputFile> output =
      mlir::openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << '\n';
    return EXIT_FAILURE;
  }

  mlir::LogicalResult result =
      mlir::MlirOptMain(output->os(), std::move(input), registry, config);
  if (mlir::succeeded(result))
    output->keep();
  return mlir::asMainReturnCode(result);
}
