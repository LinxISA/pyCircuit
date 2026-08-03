#include "acir/InitAllDialects.h"
#include "acir/InitAllPasses.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  acir::registerAllDialects(registry);
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
