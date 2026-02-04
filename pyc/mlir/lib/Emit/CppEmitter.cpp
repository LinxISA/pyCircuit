#include "pyc/Emit/CppEmitter.h"

#include "pyc/Dialect/PYC/PYCOps.h"
#include "pyc/Dialect/PYC/PYCTypes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace pyc {
namespace {

static std::string sanitizeId(llvm::StringRef s) {
  std::string out;
  out.reserve(s.size() + 1);
  auto isAlpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  auto isDigit = [](char c) { return (c >= '0' && c <= '9'); };
  auto isOk = [&](char c) { return isAlpha(c) || isDigit(c) || c == '_'; };

  for (char c : s) {
    out.push_back(isOk(c) ? c : '_');
  }
  if (out.empty() || isDigit(out.front()))
    out.insert(out.begin(), '_');
  return out;
}

static std::string cppType(Type ty) {
  if (isa<pyc::ClockType>(ty) || isa<pyc::ResetType>(ty))
    return "pyc::cpp::Wire<1>";
  auto intTy = dyn_cast<IntegerType>(ty);
  if (!intTy)
    return "pyc::cpp::Wire<1>";
  return "pyc::cpp::Wire<" + std::to_string(intTy.getWidth()) + ">";
}

static unsigned bitWidth(Type ty) {
  if (isa<pyc::ClockType>(ty) || isa<pyc::ResetType>(ty))
    return 1;
  auto intTy = dyn_cast<IntegerType>(ty);
  return intTy ? intTy.getWidth() : 0;
}

struct NameTable {
  llvm::DenseMap<Value, std::string> names;
  llvm::StringMap<unsigned> used;
  int next = 0;

  std::string unique(std::string base) {
    unsigned &n = used[base];
    n++;
    if (n == 1)
      return base;
    return base + "_" + std::to_string(n);
  }

  std::string get(Value v) {
    if (auto it = names.find(v); it != names.end())
      return it->second;
    if (Operation *def = v.getDefiningOp()) {
      if (auto nAttr = def->getAttrOfType<StringAttr>("pyc.name")) {
        std::string cand = unique(sanitizeId(nAttr.getValue()));
        names.try_emplace(v, cand);
        return cand;
      }
      // Fall back to op-based names for readability (instead of v1/v2/...).
      std::string base = sanitizeId(def->getName().getStringRef());
      if (base.empty())
        base = "v";
      base += "_" + std::to_string(++next);
      std::string cand = unique(base);
      names.try_emplace(v, cand);
      return cand;
    }
    std::string n = unique("arg_" + std::to_string(++next));
    names.try_emplace(v, n);
    return n;
  }
};

static std::string getPortName(func::FuncOp f, unsigned idx, bool isResult) {
  if (!isResult) {
    if (auto names = f->getAttrOfType<ArrayAttr>("arg_names")) {
      if (idx < names.size())
        if (auto s = dyn_cast<StringAttr>(names[idx]))
          return sanitizeId(s.getValue());
    }
    return "arg" + std::to_string(idx);
  }
  if (auto names = f->getAttrOfType<ArrayAttr>("result_names")) {
    if (idx < names.size())
      if (auto s = dyn_cast<StringAttr>(names[idx]))
        return sanitizeId(s.getValue());
  }
  return "out" + std::to_string(idx);
}

static LogicalResult emitCombAssign(Operation &op, llvm::raw_ostream &os, NameTable &nt) {
  if (auto c = dyn_cast<pyc::ConstantOp>(op)) {
    unsigned w = bitWidth(c.getType());
    if (w == 0 || w > 64)
      return c.emitError("C++ emitter only supports widths 1..64 for constants in the prototype");
    os << "    " << nt.get(c.getResult()) << " = pyc::cpp::Wire<" << w << ">("
       << c.getValueAttr().getValue().getZExtValue() << "ull);\n";
    return success();
  }
  if (auto a = dyn_cast<pyc::AliasOp>(op)) {
    os << "    " << nt.get(a.getResult()) << " = " << nt.get(a.getIn()) << ";\n";
    return success();
  }
  if (auto a = dyn_cast<pyc::AddOp>(op)) {
    os << "    " << nt.get(a.getResult()) << " = (" << nt.get(a.getLhs()) << " + " << nt.get(a.getRhs()) << ");\n";
    return success();
  }
  if (auto m = dyn_cast<pyc::MuxOp>(op)) {
    os << "    " << nt.get(m.getResult()) << " = (" << nt.get(m.getSel()) << ".toBool() ? " << nt.get(m.getA())
       << " : " << nt.get(m.getB()) << ");\n";
    return success();
  }
  if (auto s = dyn_cast<arith::SelectOp>(op)) {
    if (!s.getCondition().getType().isInteger(1))
      return s.emitError("C++ emitter only supports arith.select with i1 condition");
    os << "    " << nt.get(s.getResult()) << " = (" << nt.get(s.getCondition()) << ".toBool() ? " << nt.get(s.getTrueValue())
       << " : " << nt.get(s.getFalseValue()) << ");\n";
    return success();
  }
  if (auto a = dyn_cast<pyc::AndOp>(op)) {
    os << "    " << nt.get(a.getResult()) << " = (" << nt.get(a.getLhs()) << " & " << nt.get(a.getRhs()) << ");\n";
    return success();
  }
  if (auto o = dyn_cast<pyc::OrOp>(op)) {
    os << "    " << nt.get(o.getResult()) << " = (" << nt.get(o.getLhs()) << " | " << nt.get(o.getRhs()) << ");\n";
    return success();
  }
  if (auto x = dyn_cast<pyc::XorOp>(op)) {
    os << "    " << nt.get(x.getResult()) << " = (" << nt.get(x.getLhs()) << " ^ " << nt.get(x.getRhs()) << ");\n";
    return success();
  }
  if (auto n = dyn_cast<pyc::NotOp>(op)) {
    os << "    " << nt.get(n.getResult()) << " = (~" << nt.get(n.getIn()) << ");\n";
    return success();
  }
  if (auto e = dyn_cast<pyc::EqOp>(op)) {
    os << "    " << nt.get(e.getResult()) << " = pyc::cpp::Wire<1>((" << nt.get(e.getLhs()) << " == "
       << nt.get(e.getRhs()) << ") ? 1u : 0u);\n";
    return success();
  }
  if (auto t = dyn_cast<pyc::TruncOp>(op)) {
    unsigned iw = bitWidth(t.getIn().getType());
    unsigned ow = bitWidth(t.getResult().getType());
    if (iw == 0 || ow == 0 || iw > 64 || ow > 64)
      return t.emitError("C++ emitter only supports widths 1..64 for trunc in the prototype");
    os << "    " << nt.get(t.getResult()) << " = pyc::cpp::trunc<" << ow << ", " << iw << ">(" << nt.get(t.getIn())
       << ");\n";
    return success();
  }
  if (auto z = dyn_cast<pyc::ZextOp>(op)) {
    unsigned iw = bitWidth(z.getIn().getType());
    unsigned ow = bitWidth(z.getResult().getType());
    if (iw == 0 || ow == 0 || iw > 64 || ow > 64)
      return z.emitError("C++ emitter only supports widths 1..64 for zext in the prototype");
    os << "    " << nt.get(z.getResult()) << " = pyc::cpp::zext<" << ow << ", " << iw << ">(" << nt.get(z.getIn())
       << ");\n";
    return success();
  }
  if (auto s = dyn_cast<pyc::SextOp>(op)) {
    unsigned iw = bitWidth(s.getIn().getType());
    unsigned ow = bitWidth(s.getResult().getType());
    if (iw == 0 || ow == 0 || iw > 64 || ow > 64)
      return s.emitError("C++ emitter only supports widths 1..64 for sext in the prototype");
    os << "    " << nt.get(s.getResult()) << " = pyc::cpp::sext<" << ow << ", " << iw << ">(" << nt.get(s.getIn())
       << ");\n";
    return success();
  }
  if (auto ex = dyn_cast<pyc::ExtractOp>(op)) {
    unsigned iw = bitWidth(ex.getIn().getType());
    unsigned ow = bitWidth(ex.getResult().getType());
    if (iw == 0 || ow == 0 || iw > 64 || ow > 64)
      return ex.emitError("C++ emitter only supports widths 1..64 for extract in the prototype");
    os << "    " << nt.get(ex.getResult()) << " = pyc::cpp::extract<" << ow << ", " << iw << ">("
       << nt.get(ex.getIn()) << ", " << ex.getLsbAttr().getInt() << "u);\n";
    return success();
  }
  if (auto sh = dyn_cast<pyc::ShliOp>(op)) {
    unsigned w = bitWidth(sh.getResult().getType());
    if (w == 0 || w > 64)
      return sh.emitError("C++ emitter only supports widths 1..64 for shli in the prototype");
    os << "    " << nt.get(sh.getResult()) << " = pyc::cpp::Wire<" << w << ">(" << nt.get(sh.getIn())
       << ".value() << " << sh.getAmountAttr().getInt() << "ull);\n";
    return success();
  }
  if (auto c = dyn_cast<pyc::ConcatOp>(op)) {
    unsigned w = bitWidth(c.getResult().getType());
    if (w == 0 || w > 64)
      return c.emitError("C++ emitter only supports total widths 1..64 for concat in the prototype");
    os << "    " << nt.get(c.getResult()) << " = pyc::cpp::concat(";
    for (auto [i, v] : llvm::enumerate(c.getInputs())) {
      if (i)
        os << ", ";
      os << nt.get(v);
    }
    os << ");\n";
    return success();
  }
  return op.emitError("unsupported combinational op for C++ emission");
}

static LogicalResult emitCombMethod(pyc::CombOp comb, llvm::raw_ostream &os, NameTable &nt, unsigned idx) {
  if (comb.getBody().empty())
    return comb.emitError("pyc.comb must have a non-empty region");

  Block &b = comb.getBody().front();
  if (b.getNumArguments() != comb.getNumOperands())
    return comb.emitError("pyc.comb body block argument count must match inputs");

  // Map region args to the corresponding input values (by name).
  for (auto [i, arg] : llvm::enumerate(b.getArguments()))
    nt.names.try_emplace(arg, nt.get(comb.getInputs()[i]));

  os << "  inline void eval_comb_" << idx << "() {\n";
  for (Operation &op : b) {
    if (isa<pyc::YieldOp>(op))
      break;
    if (failed(emitCombAssign(op, os, nt)))
      return failure();
  }

  auto y = dyn_cast_or_null<pyc::YieldOp>(b.getTerminator());
  if (!y)
    return comb.emitError("pyc.comb must terminate with pyc.yield");
  if (y.getNumOperands() != comb.getNumResults())
    return comb.emitError("pyc.yield operand count must match pyc.comb results");

  for (auto [i, v] : llvm::enumerate(y.getOperands()))
    os << "    " << nt.get(comb.getResult(i)) << " = " << nt.get(v) << ";\n";
  os << "  }\n\n";
  return success();
}

static LogicalResult emitFunc(func::FuncOp f, llvm::raw_ostream &os) {
  NameTable nt;

  if (!llvm::hasSingleElement(f.getBody()))
    return f.emitError("C++ emitter currently supports single-block functions only");

  Block &top = f.getBody().front();

  std::string structName = sanitizeId(f.getSymName());
  os << "struct " << structName << " {\n";

  // Ports.
  for (auto [i, arg] : llvm::enumerate(f.getArguments())) {
    std::string name = getPortName(f, i, /*isResult=*/false);
    nt.names.try_emplace(arg, name);
    os << "  " << cppType(arg.getType()) << " " << name << "{};\n";
  }
  for (unsigned i = 0; i < f.getNumResults(); ++i) {
    os << "  " << cppType(f.getResultTypes()[i]) << " " << getPortName(f, i, /*isResult=*/true) << "{};\n";
  }
  os << "\n";

  // Internal wires for op results (including inside pyc.comb regions).
  f.walk([&](Operation *op) {
    for (Value r : op->getResults())
      os << "  " << cppType(r.getType()) << " " << nt.get(r) << "{};\n";
  });
  os << "\n";

  // Sequential primitive instances.
  llvm::SmallVector<pyc::RegOp> regs;
  llvm::SmallVector<pyc::FifoOp> fifos;
  llvm::SmallVector<pyc::ByteMemOp> byteMems;
  llvm::SmallVector<pyc::CombOp> combs;

  for (Operation &op : top) {
    if (auto r = dyn_cast<pyc::RegOp>(op))
      regs.push_back(r);
    else if (auto fifo = dyn_cast<pyc::FifoOp>(op))
      fifos.push_back(fifo);
    else if (auto mem = dyn_cast<pyc::ByteMemOp>(op))
      byteMems.push_back(mem);
    else if (auto comb = dyn_cast<pyc::CombOp>(op))
      combs.push_back(comb);
  }

  llvm::DenseMap<Operation *, std::string> byteMemInstName;

  for (auto r : regs) {
    unsigned w = bitWidth(r.getQ().getType());
    if (w == 0 || w > 64)
      return r.emitError("C++ emitter only supports reg widths 1..64 in the prototype");
    os << "  pyc::cpp::pyc_reg<" << w << "> " << nt.get(r.getQ()) << "_inst;\n";
  }
  for (auto fifo : fifos) {
    unsigned w = bitWidth(fifo.getOutData().getType());
    if (w == 0 || w > 64)
      return fifo.emitError("C++ emitter only supports fifo widths 1..64 in the prototype");
    auto depthAttr = fifo->getAttrOfType<IntegerAttr>("depth");
    if (!depthAttr)
      return fifo.emitError("missing integer attribute `depth`");
    auto depth = depthAttr.getValue().getZExtValue();
    os << "  pyc::cpp::pyc_fifo<" << w << ", " << depth << "> " << nt.get(fifo.getInReady()) << "_inst;\n";
  }
  for (auto mem : byteMems) {
    auto addrTy = dyn_cast<IntegerType>(mem.getRaddr().getType());
    auto dataTy = dyn_cast<IntegerType>(mem.getRdata().getType());
    if (!addrTy || !dataTy)
      return mem.emitError("C++ emitter only supports integer byte_mem types");
    unsigned addrW = addrTy.getWidth();
    unsigned dataW = dataTy.getWidth();
    if (addrW == 0 || addrW > 64)
      return mem.emitError("C++ emitter only supports byte_mem addr widths 1..64 in the prototype");
    if (dataW == 0 || dataW > 64)
      return mem.emitError("C++ emitter only supports byte_mem data widths 1..64 in the prototype");

    auto depthAttr = mem->getAttrOfType<IntegerAttr>("depth");
    if (!depthAttr)
      return mem.emitError("missing integer attribute `depth`");
    auto depth = depthAttr.getValue().getZExtValue();

    std::string instName = nt.get(mem.getRdata()) + "_inst";
    if (auto nameAttr = mem->getAttrOfType<StringAttr>("name"))
      instName = sanitizeId(nameAttr.getValue());
    byteMemInstName.try_emplace(mem.getOperation(), instName);

    os << "  pyc::cpp::pyc_byte_mem<" << addrW << ", " << dataW << ", " << depth << "> " << instName << ";\n";
  }
  os << "\n";

  // Constructor (wire members default-initialize to 0).
  os << "  " << structName << "()";
  bool firstInit = true;
  for (auto r : regs) {
    os << (firstInit ? " :\n" : ",\n");
    firstInit = false;
    os << "      " << nt.get(r.getQ()) << "_inst(" << nt.get(r.getClk()) << ", " << nt.get(r.getRst()) << ", "
       << nt.get(r.getEn()) << ", " << nt.get(r.getNext()) << ", " << nt.get(r.getInit()) << ", " << nt.get(r.getQ())
       << ")";
  }
  for (auto fifo : fifos) {
    os << (firstInit ? " :\n" : ",\n");
    firstInit = false;
    unsigned w = bitWidth(fifo.getOutData().getType());
    auto depth = fifo->getAttrOfType<IntegerAttr>("depth").getValue().getZExtValue();
    (void)w;
    (void)depth;
    os << "      " << nt.get(fifo.getInReady()) << "_inst(" << nt.get(fifo.getClk()) << ", " << nt.get(fifo.getRst())
       << ", " << nt.get(fifo.getInValid()) << ", " << nt.get(fifo.getInReady()) << ", " << nt.get(fifo.getInData())
       << ", " << nt.get(fifo.getOutValid()) << ", " << nt.get(fifo.getOutReady()) << ", " << nt.get(fifo.getOutData())
       << ")";
  }
  for (auto mem : byteMems) {
    os << (firstInit ? " :\n" : ",\n");
    firstInit = false;
    std::string instName = byteMemInstName.lookup(mem.getOperation());
    os << "      " << instName << "(" << nt.get(mem.getClk()) << ", " << nt.get(mem.getRst()) << ", " << nt.get(mem.getRaddr())
       << ", " << nt.get(mem.getRdata()) << ", " << nt.get(mem.getWvalid()) << ", " << nt.get(mem.getWaddr()) << ", "
       << nt.get(mem.getWdata()) << ", " << nt.get(mem.getWstrb()) << ")";
  }
  os << " {\n";
  os << "    eval();\n";
  os << "  }\n\n";

  // Emit fused comb helpers.
  for (auto [i, comb] : llvm::enumerate(combs)) {
    if (failed(emitCombMethod(comb, os, nt, static_cast<unsigned>(i))))
      return failure();
  }

  llvm::DenseMap<Operation *, unsigned> combIndex;
  for (auto [i, comb] : llvm::enumerate(combs))
    combIndex.try_emplace(comb.getOperation(), static_cast<unsigned>(i));

  // eval_comb_pass(): evaluate all combinational ops/assigns.
  //
  // Note: The IR is allowed to have "late" pyc.assign ops (e.g. queue wrappers
  // that defer wiring). To keep C++ simulation correct, eval() runs a small
  // fixed-point iteration that alternates comb evaluation and primitive eval.
  os << "  inline void eval_comb_pass() {\n";
  for (Operation &op : top) {
    if (isa<func::ReturnOp>(op))
      continue;
    if (isa<pyc::WireOp>(op))
      continue;
    if (auto a = dyn_cast<pyc::AssignOp>(op)) {
      os << "    " << nt.get(a.getDst()) << " = " << nt.get(a.getSrc()) << ";\n";
      continue;
    }
    if (auto comb = dyn_cast<pyc::CombOp>(op)) {
      os << "    eval_comb_" << combIndex.lookup(comb.getOperation()) << "();\n";
      continue;
    }
    if (auto c = dyn_cast<pyc::ConstantOp>(op)) {
      if (failed(emitCombAssign(op, os, nt)))
        return failure();
      continue;
    }
    if (isa<pyc::AddOp,
            pyc::MuxOp,
            pyc::AndOp,
            pyc::OrOp,
            pyc::XorOp,
            pyc::NotOp,
            pyc::ConcatOp,
            pyc::AliasOp,
            pyc::EqOp,
            pyc::TruncOp,
            pyc::ZextOp,
            pyc::SextOp,
            pyc::ExtractOp,
            pyc::ShliOp,
            arith::SelectOp>(op)) {
      if (failed(emitCombAssign(op, os, nt)))
        return failure();
      continue;
    }
    if (isa<pyc::FifoOp, pyc::ByteMemOp, pyc::RegOp>(op)) {
      // Primitives are evaluated in eval(), and regs only tick.
      continue;
    }
    return op.emitError("unsupported op for C++ emission: ") << op.getName();
  }
  os << "  }\n\n";

  // eval(): alternate comb and primitive eval to tolerate non-topologically-
  // ordered assigns (prototype netlist scheduling).
  os << "  void eval() {\n";
  os << "    eval_comb_pass();\n";

  unsigned numPrims = static_cast<unsigned>(fifos.size() + byteMems.size());
  if (numPrims > 0) {
    os << "    for (unsigned _i = 0; _i < " << numPrims << "u; ++_i) {\n";
    for (Operation &op : top) {
      if (auto fifo = dyn_cast<pyc::FifoOp>(op)) {
        os << "      " << nt.get(fifo.getInReady()) << "_inst.eval();\n";
        continue;
      }
      if (auto mem = dyn_cast<pyc::ByteMemOp>(op)) {
        os << "      " << byteMemInstName.lookup(mem.getOperation()) << ".eval();\n";
        continue;
      }
    }
    os << "      eval_comb_pass();\n";
    os << "    }\n";
  }

  // Connect return values to output ports.
  auto ret = dyn_cast_or_null<func::ReturnOp>(f.getBody().front().getTerminator());
  if (!ret)
    return f.emitError("missing return");
  for (auto [i, v] : llvm::enumerate(ret.getOperands()))
    os << "    " << getPortName(f, i, /*isResult=*/true) << " = " << nt.get(v) << ";\n";

  os << "  }\n\n";

  // tick(): tick sequential primitives (posedge detection is inside primitives).
  os << "  void tick() {\n";
  os << "    // Two-phase update: compute next state for all sequential elements,\n";
  os << "    // then commit together. This avoids ordering artifacts between regs.\n";
  os << "    // Phase 1: compute.\n";
  for (Operation &op : top) {
    if (auto r = dyn_cast<pyc::RegOp>(op)) {
      os << "    " << nt.get(r.getQ()) << "_inst.tick_compute();\n";
      continue;
    }
    if (auto fifo = dyn_cast<pyc::FifoOp>(op)) {
      os << "    " << nt.get(fifo.getInReady()) << "_inst.tick_compute();\n";
      continue;
    }
    if (auto mem = dyn_cast<pyc::ByteMemOp>(op)) {
      os << "    " << byteMemInstName.lookup(mem.getOperation()) << ".tick_compute();\n";
      continue;
    }
  }
  os << "    // Phase 2: commit.\n";
  for (Operation &op : top) {
    if (auto r = dyn_cast<pyc::RegOp>(op)) {
      os << "    " << nt.get(r.getQ()) << "_inst.tick_commit();\n";
      continue;
    }
    if (auto fifo = dyn_cast<pyc::FifoOp>(op)) {
      os << "    " << nt.get(fifo.getInReady()) << "_inst.tick_commit();\n";
      continue;
    }
    if (auto mem = dyn_cast<pyc::ByteMemOp>(op)) {
      os << "    " << byteMemInstName.lookup(mem.getOperation()) << ".tick_commit();\n";
      continue;
    }
  }
  os << "  }\n";

  os << "};\n\n";
  return success();
}

} // namespace

LogicalResult emitCpp(ModuleOp module, llvm::raw_ostream &os, const CppEmitterOptions &) {
  os << "// pyCircuit C++ emission (prototype)\n";
  os << "#include <pyc/cpp/pyc_sim.hpp>\n\n";
  os << "namespace pyc::gen {\n\n";

  for (auto f : module.getOps<func::FuncOp>()) {
    if (failed(emitFunc(f, os)))
      return failure();
  }

  os << "} // namespace pyc::gen\n";
  return success();
}

} // namespace pyc
