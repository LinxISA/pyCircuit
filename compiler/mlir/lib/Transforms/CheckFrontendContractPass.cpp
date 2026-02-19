#include "pyc/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include <string>
#include <utility>

using namespace mlir;

namespace pyc {
namespace {

class CheckFrontendContractPass : public PassWrapper<CheckFrontendContractPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CheckFrontendContractPass)

  explicit CheckFrontendContractPass(std::string requiredApiVersion = "v3.4")
      : requiredApi(std::move(requiredApiVersion)) {}

  StringRef getArgument() const override { return "pyc-check-frontend-contract"; }
  StringRef getDescription() const override {
    return "Verify required frontend API contract attrs are present and match the required version";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    bool ok = true;
    auto emitModule = [&](llvm::StringRef code, llvm::StringRef msg, llvm::StringRef hint) {
      auto d = module.emitError();
      d << "[" << code << "] " << msg;
      if (!hint.empty())
        d << " (hint: " << hint << ")";
    };

    auto modApi = module->getAttrOfType<StringAttr>("pyc.frontend.api");
    if (!modApi) {
      emitModule("PYC901", "missing required module attr `pyc.frontend.api`",
                 "compile with pyCircuit frontend v3.4 and keep module attrs");
      ok = false;
    } else if (modApi.getValue() != requiredApi) {
      auto d = module.emitError();
      d << "[PYC902] frontend api mismatch: expected `" << requiredApi << "`, got `" << modApi.getValue()
        << "` (hint: regenerate .pyc with matching frontend API epoch)";
      ok = false;
    }

    module.walk([&](func::FuncOp f) {
      auto checkStrAttr = [&](StringRef name, llvm::StringRef code, llvm::StringRef hint) -> StringAttr {
        auto attr = f->getAttrOfType<StringAttr>(name);
        if (!attr) {
          auto d = f.emitError();
          d << "[" << code << "] missing required func attr `" << name << "`";
          if (!hint.empty())
            d << " (hint: " << hint << ")";
          ok = false;
        }
        return attr;
      };

      auto api = checkStrAttr("pyc.frontend.api", "PYC903", "regenerate function with pyCircuit v3.4 frontend");
      auto kind = checkStrAttr("pyc.kind", "PYC904", "frontend must stamp symbol kind metadata");
      auto inl = checkStrAttr("pyc.inline", "PYC905", "frontend must stamp inline metadata");
      (void)checkStrAttr("pyc.params", "PYC906", "frontend must stamp canonical specialization params");
      (void)checkStrAttr("pyc.base", "PYC907", "frontend must stamp canonical base symbol name");

      if (api && api.getValue() != requiredApi) {
        f.emitError() << "[PYC908] func frontend api mismatch: expected `" << requiredApi << "`, got `"
                      << api.getValue() << "` (hint: regenerate .pyc with matching frontend API epoch)";
        ok = false;
      }

      if (kind) {
        auto k = kind.getValue();
        if (k != "module" && k != "function" && k != "template") {
          f.emitError() << "[PYC909] invalid `pyc.kind` value: " << k
                        << " (hint: allowed values are module/function/template)";
          ok = false;
        }
      }

      if (inl) {
        auto v = inl.getValue();
        if (v != "true" && v != "false") {
          f.emitError() << "[PYC910] invalid `pyc.inline` value: " << v
                        << " (hint: allowed values are true|false)";
          ok = false;
        }
      }
    });

    if (!ok)
      signalPassFailure();
  }

private:
  std::string requiredApi;
};

} // namespace

std::unique_ptr<::mlir::Pass> createCheckFrontendContractPass(std::string requiredApi) {
  return std::make_unique<CheckFrontendContractPass>(std::move(requiredApi));
}

static PassRegistration<CheckFrontendContractPass> pass;

} // namespace pyc
