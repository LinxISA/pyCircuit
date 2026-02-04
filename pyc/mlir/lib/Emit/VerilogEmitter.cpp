#include "pyc/Emit/VerilogEmitter.h"

#include "pyc/Dialect/PYC/PYCOps.h"
#include "pyc/Dialect/PYC/PYCTypes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

using namespace mlir;

namespace pyc {
namespace {

static std::string svType(Type ty) {
  if (isa<pyc::ClockType>(ty) || isa<pyc::ResetType>(ty))
    return "logic";
  auto intTy = dyn_cast<IntegerType>(ty);
  if (!intTy)
    return "logic";
  if (intTy.getWidth() == 1)
    return "logic";
  return "logic [" + std::to_string(intTy.getWidth() - 1) + ":0]";
}

static std::string svLiteral(IntegerAttr a, Type dstTy) {
  auto intTy = dyn_cast<IntegerType>(dstTy);
  if (!intTy)
    return "0";
  unsigned w = intTy.getWidth();
  return std::to_string(w) + "'d" + std::to_string(a.getValue().getZExtValue());
}

static std::string sanitizeId(llvm::StringRef s) {
  std::string out;
  out.reserve(s.size() + 1);
  auto isAlpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  auto isDigit = [](char c) { return (c >= '0' && c <= '9'); };
  auto isOk = [&](char c) { return isAlpha(c) || isDigit(c) || c == '_'; };

  for (char c : s)
    out.push_back(isOk(c) ? c : '_');
  if (out.empty() || isDigit(out.front()))
    out.insert(out.begin(), '_');
  return out;
}

struct NameTable {
  DenseMap<Value, std::string> names;
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
      // Make the name stable and unique within this module.
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
  // Prefer explicit name lists emitted by the Python frontend.
  if (!isResult) {
    if (auto names = f->getAttrOfType<ArrayAttr>("arg_names")) {
      if (idx < names.size())
        if (auto s = dyn_cast<StringAttr>(names[idx]))
          return s.getValue().str();
    }
    return "arg" + std::to_string(idx);
  }
  if (auto names = f->getAttrOfType<ArrayAttr>("result_names")) {
    if (idx < names.size())
      if (auto s = dyn_cast<StringAttr>(names[idx]))
        return s.getValue().str();
  }
  return "out" + std::to_string(idx);
}

static LogicalResult emitComb(pyc::CombOp comb, raw_ostream &os, NameTable &nt) {
  if (comb.getBody().empty())
    return comb.emitError("pyc.comb must have a non-empty region");
  Block &b = comb.getBody().front();
  if (b.getNumArguments() != comb.getNumOperands())
    return comb.emitError("pyc.comb region block args must match input operand count");

  for (auto [i, arg] : llvm::enumerate(b.getArguments()))
    nt.names.try_emplace(arg, nt.get(comb.getInputs()[i]));

  for (Operation &op : b) {
    if (isa<pyc::YieldOp>(op))
      break;

    if (auto c = dyn_cast<pyc::ConstantOp>(op)) {
      os << "assign " << nt.get(c.getResult()) << " = " << svLiteral(c.getValueAttr(), c.getType()) << ";\n";
      continue;
    }
    if (auto a = dyn_cast<pyc::AliasOp>(op)) {
      os << "assign " << nt.get(a.getResult()) << " = " << nt.get(a.getIn()) << ";\n";
      continue;
    }
    if (auto a = dyn_cast<pyc::AddOp>(op)) {
      os << "assign " << nt.get(a.getResult()) << " = (" << nt.get(a.getLhs()) << " + " << nt.get(a.getRhs())
         << ");\n";
      continue;
    }
    if (auto m = dyn_cast<pyc::MuxOp>(op)) {
      os << "assign " << nt.get(m.getResult()) << " = (" << nt.get(m.getSel()) << " ? " << nt.get(m.getA())
         << " : " << nt.get(m.getB()) << ");\n";
      continue;
    }
    if (auto s = dyn_cast<arith::SelectOp>(op)) {
      if (!s.getCondition().getType().isInteger(1))
        return s.emitError("verilog emitter only supports arith.select with i1 condition");
      os << "assign " << nt.get(s.getResult()) << " = (" << nt.get(s.getCondition()) << " ? " << nt.get(s.getTrueValue())
         << " : " << nt.get(s.getFalseValue()) << ");\n";
      continue;
    }
    if (auto a = dyn_cast<pyc::AndOp>(op)) {
      os << "assign " << nt.get(a.getResult()) << " = (" << nt.get(a.getLhs()) << " & " << nt.get(a.getRhs())
         << ");\n";
      continue;
    }
    if (auto o = dyn_cast<pyc::OrOp>(op)) {
      os << "assign " << nt.get(o.getResult()) << " = (" << nt.get(o.getLhs()) << " | " << nt.get(o.getRhs())
         << ");\n";
      continue;
    }
    if (auto x = dyn_cast<pyc::XorOp>(op)) {
      os << "assign " << nt.get(x.getResult()) << " = (" << nt.get(x.getLhs()) << " ^ " << nt.get(x.getRhs())
         << ");\n";
      continue;
    }
    if (auto n = dyn_cast<pyc::NotOp>(op)) {
      os << "assign " << nt.get(n.getResult()) << " = (~" << nt.get(n.getIn()) << ");\n";
      continue;
    }
    if (auto e = dyn_cast<pyc::EqOp>(op)) {
      os << "assign " << nt.get(e.getResult()) << " = (" << nt.get(e.getLhs()) << " == " << nt.get(e.getRhs())
         << ");\n";
      continue;
    }
    if (auto t = dyn_cast<pyc::TruncOp>(op)) {
      auto outTy = dyn_cast<IntegerType>(t.getResult().getType());
      if (!outTy)
        return t.emitError("verilog emitter only supports integer trunc");
      unsigned w = outTy.getWidth();
      if (w == 1)
        os << "assign " << nt.get(t.getResult()) << " = " << nt.get(t.getIn()) << "[0];\n";
      else
        os << "assign " << nt.get(t.getResult()) << " = " << nt.get(t.getIn()) << "[" << (w - 1) << ":0];\n";
      continue;
    }
    if (auto z = dyn_cast<pyc::ZextOp>(op)) {
      auto inTy = dyn_cast<IntegerType>(z.getIn().getType());
      auto outTy = dyn_cast<IntegerType>(z.getResult().getType());
      if (!inTy || !outTy)
        return z.emitError("verilog emitter only supports integer zext");
      unsigned iw = inTy.getWidth();
      unsigned ow = outTy.getWidth();
      if (ow == iw) {
        os << "assign " << nt.get(z.getResult()) << " = " << nt.get(z.getIn()) << ";\n";
      } else {
        os << "assign " << nt.get(z.getResult()) << " = {{" << (ow - iw) << "{1'b0}}, " << nt.get(z.getIn())
           << "};\n";
      }
      continue;
    }
    if (auto s = dyn_cast<pyc::SextOp>(op)) {
      auto inTy = dyn_cast<IntegerType>(s.getIn().getType());
      auto outTy = dyn_cast<IntegerType>(s.getResult().getType());
      if (!inTy || !outTy)
        return s.emitError("verilog emitter only supports integer sext");
      unsigned iw = inTy.getWidth();
      unsigned ow = outTy.getWidth();
      if (ow == iw) {
        os << "assign " << nt.get(s.getResult()) << " = " << nt.get(s.getIn()) << ";\n";
      } else {
        os << "assign " << nt.get(s.getResult()) << " = {{" << (ow - iw) << "{" << nt.get(s.getIn()) << "["
           << (iw - 1) << "]}}, " << nt.get(s.getIn()) << "};\n";
      }
      continue;
    }
    if (auto ex = dyn_cast<pyc::ExtractOp>(op)) {
      auto inTy = dyn_cast<IntegerType>(ex.getIn().getType());
      auto outTy = dyn_cast<IntegerType>(ex.getResult().getType());
      if (!inTy || !outTy)
        return ex.emitError("verilog emitter only supports integer extract");
      unsigned ow = outTy.getWidth();
      std::int64_t lsb = ex.getLsbAttr().getInt();
      if (ow == 1) {
        os << "assign " << nt.get(ex.getResult()) << " = " << nt.get(ex.getIn()) << "[" << lsb << "];\n";
      } else {
        os << "assign " << nt.get(ex.getResult()) << " = " << nt.get(ex.getIn()) << "[" << (lsb + ow - 1) << ":"
           << lsb << "];\n";
      }
      continue;
    }
    if (auto sh = dyn_cast<pyc::ShliOp>(op)) {
      os << "assign " << nt.get(sh.getResult()) << " = (" << nt.get(sh.getIn()) << " << "
         << sh.getAmountAttr().getInt() << ");\n";
      continue;
    }
    if (auto c = dyn_cast<pyc::ConcatOp>(op)) {
      os << "assign " << nt.get(c.getResult()) << " = {";
      for (auto [i, v] : llvm::enumerate(c.getInputs())) {
        if (i)
          os << ", ";
        os << nt.get(v);
      }
      os << "};\n";
      continue;
    }

    return op.emitError("unsupported op inside pyc.comb for verilog emission");
  }

  auto yield = dyn_cast_or_null<pyc::YieldOp>(b.getTerminator());
  if (!yield)
    return comb.emitError("pyc.comb must terminate with pyc.yield");
  if (yield.getNumOperands() != comb.getNumResults())
    return comb.emitError("pyc.yield operand count must match pyc.comb results");

  for (auto [i, v] : llvm::enumerate(yield.getOperands()))
    os << "assign " << nt.get(comb.getResult(i)) << " = " << nt.get(v) << ";\n";

  return success();
}

static LogicalResult emitFunc(func::FuncOp f, raw_ostream &os, const VerilogEmitterOptions &opts) {
  NameTable nt;

  os << "// Generated by pyc-compile (pyCircuit)\n";
  os << "// Module: " << f.getSymName() << "\n\n";

  // Module header.
  os << "module " << f.getSymName() << " (\n";
  for (auto [i, arg] : llvm::enumerate(f.getArguments())) {
    std::string dir = "input ";
    os << "  " << dir << svType(arg.getType()) << " " << getPortName(f, i, /*isResult=*/false);
    os << ((i + 1 == f.getNumArguments() && f.getNumResults() == 0) ? "\n" : ",\n");
    nt.names.try_emplace(arg, getPortName(f, i, /*isResult=*/false));
  }
  for (unsigned i = 0; i < f.getNumResults(); ++i) {
    std::string dir = "output ";
    os << "  " << dir << svType(f.getResultTypes()[i]) << " " << getPortName(f, i, /*isResult=*/true);
    os << ((i + 1 == f.getNumResults()) ? "\n" : ",\n");
  }
  os << ");\n\n";

  // Declare internal nets for op results.
  f.walk([&](Operation *op) {
    for (Value r : op->getResults()) {
      std::string n = nt.get(r);
      os << svType(r.getType()) << " " << n << ";";
      if (auto nAttr = op->getAttrOfType<StringAttr>("pyc.name")) {
        os << " // pyc.name=\"" << nAttr.getValue() << "\"";
      } else {
        os << " // op=" << op->getName().getStringRef();
      }
      os << "\n";
    }
  });
  os << "\n";

  // Emit combinational assigns and sequential blocks.
  for (Block &b : f.getBody()) {
    for (Operation &op : b) {
      if (isa<func::ReturnOp>(op))
        continue;

      if (auto c = dyn_cast<pyc::ConstantOp>(op)) {
        os << "assign " << nt.get(c.getResult()) << " = " << svLiteral(c.getValueAttr(), c.getType()) << ";\n";
        continue;
      }
      if (isa<pyc::WireOp>(op)) {
        // Declaration is handled by the internal net pass; assignment comes from `pyc.assign`.
        continue;
      }
      if (auto a = dyn_cast<pyc::AliasOp>(op)) {
        os << "assign " << nt.get(a.getResult()) << " = " << nt.get(a.getIn()) << ";\n";
        continue;
      }
      if (auto a = dyn_cast<pyc::AssignOp>(op)) {
        os << "assign " << nt.get(a.getDst()) << " = " << nt.get(a.getSrc()) << ";\n";
        continue;
      }
      if (auto comb = dyn_cast<pyc::CombOp>(op)) {
        if (failed(emitComb(comb, os, nt)))
          return failure();
        continue;
      }
      if (auto s = dyn_cast<arith::SelectOp>(op)) {
        if (!s.getCondition().getType().isInteger(1))
          return s.emitError("verilog emitter only supports arith.select with i1 condition");
        os << "assign " << nt.get(s.getResult()) << " = (" << nt.get(s.getCondition()) << " ? " << nt.get(s.getTrueValue())
           << " : " << nt.get(s.getFalseValue()) << ");\n";
        continue;
      }
      if (auto e = dyn_cast<pyc::EqOp>(op)) {
        os << "assign " << nt.get(e.getResult()) << " = (" << nt.get(e.getLhs()) << " == " << nt.get(e.getRhs())
           << ");\n";
        continue;
      }
      if (auto t = dyn_cast<pyc::TruncOp>(op)) {
        auto outTy = dyn_cast<IntegerType>(t.getResult().getType());
        if (!outTy)
          return t.emitError("verilog emitter only supports integer trunc");
        unsigned w = outTy.getWidth();
        if (w == 1)
          os << "assign " << nt.get(t.getResult()) << " = " << nt.get(t.getIn()) << "[0];\n";
        else
          os << "assign " << nt.get(t.getResult()) << " = " << nt.get(t.getIn()) << "[" << (w - 1) << ":0];\n";
        continue;
      }
      if (auto z = dyn_cast<pyc::ZextOp>(op)) {
        auto inTy = dyn_cast<IntegerType>(z.getIn().getType());
        auto outTy = dyn_cast<IntegerType>(z.getResult().getType());
        if (!inTy || !outTy)
          return z.emitError("verilog emitter only supports integer zext");
        unsigned iw = inTy.getWidth();
        unsigned ow = outTy.getWidth();
        if (ow == iw) {
          os << "assign " << nt.get(z.getResult()) << " = " << nt.get(z.getIn()) << ";\n";
        } else {
          os << "assign " << nt.get(z.getResult()) << " = {{" << (ow - iw) << "{1'b0}}, " << nt.get(z.getIn())
             << "};\n";
        }
        continue;
      }
      if (auto s = dyn_cast<pyc::SextOp>(op)) {
        auto inTy = dyn_cast<IntegerType>(s.getIn().getType());
        auto outTy = dyn_cast<IntegerType>(s.getResult().getType());
        if (!inTy || !outTy)
          return s.emitError("verilog emitter only supports integer sext");
        unsigned iw = inTy.getWidth();
        unsigned ow = outTy.getWidth();
        if (ow == iw) {
          os << "assign " << nt.get(s.getResult()) << " = " << nt.get(s.getIn()) << ";\n";
        } else {
          os << "assign " << nt.get(s.getResult()) << " = {{" << (ow - iw) << "{" << nt.get(s.getIn()) << "["
             << (iw - 1) << "]}}, " << nt.get(s.getIn()) << "};\n";
        }
        continue;
      }
      if (auto ex = dyn_cast<pyc::ExtractOp>(op)) {
        auto outTy = dyn_cast<IntegerType>(ex.getResult().getType());
        if (!outTy)
          return ex.emitError("verilog emitter only supports integer extract");
        unsigned ow = outTy.getWidth();
        std::int64_t lsb = ex.getLsbAttr().getInt();
        if (ow == 1) {
          os << "assign " << nt.get(ex.getResult()) << " = " << nt.get(ex.getIn()) << "[" << lsb << "];\n";
        } else {
          os << "assign " << nt.get(ex.getResult()) << " = " << nt.get(ex.getIn()) << "[" << (lsb + ow - 1) << ":"
             << lsb << "];\n";
        }
        continue;
      }
      if (auto sh = dyn_cast<pyc::ShliOp>(op)) {
        os << "assign " << nt.get(sh.getResult()) << " = (" << nt.get(sh.getIn()) << " << "
           << sh.getAmountAttr().getInt() << ");\n";
        continue;
      }
      if (auto c = dyn_cast<pyc::ConcatOp>(op)) {
        os << "assign " << nt.get(c.getResult()) << " = {";
        for (auto [i, v] : llvm::enumerate(c.getInputs())) {
          if (i)
            os << ", ";
          os << nt.get(v);
        }
        os << "};\n";
        continue;
      }
      if (auto a = dyn_cast<pyc::AddOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(a.getResult().getType());
        if (!yTy)
          return a.emitError("verilog emitter only supports integer add data type");
        os << "pyc_add #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(a.getResult()) << "_inst (\n";
        os << "  .a(" << nt.get(a.getLhs()) << "),\n";
        os << "  .b(" << nt.get(a.getRhs()) << "),\n";
        os << "  .y(" << nt.get(a.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto m = dyn_cast<pyc::MuxOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(m.getResult().getType());
        if (!yTy)
          return m.emitError("verilog emitter only supports integer mux data type");
        os << "pyc_mux #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(m.getResult()) << "_inst (\n";
        os << "  .sel(" << nt.get(m.getSel()) << "),\n";
        os << "  .a(" << nt.get(m.getA()) << "),\n";
        os << "  .b(" << nt.get(m.getB()) << "),\n";
        os << "  .y(" << nt.get(m.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto a = dyn_cast<pyc::AndOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(a.getResult().getType());
        if (!yTy)
          return a.emitError("verilog emitter only supports integer and data type");
        os << "pyc_and #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(a.getResult()) << "_inst (\n";
        os << "  .a(" << nt.get(a.getLhs()) << "),\n";
        os << "  .b(" << nt.get(a.getRhs()) << "),\n";
        os << "  .y(" << nt.get(a.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto o = dyn_cast<pyc::OrOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(o.getResult().getType());
        if (!yTy)
          return o.emitError("verilog emitter only supports integer or data type");
        os << "pyc_or #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(o.getResult()) << "_inst (\n";
        os << "  .a(" << nt.get(o.getLhs()) << "),\n";
        os << "  .b(" << nt.get(o.getRhs()) << "),\n";
        os << "  .y(" << nt.get(o.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto x = dyn_cast<pyc::XorOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(x.getResult().getType());
        if (!yTy)
          return x.emitError("verilog emitter only supports integer xor data type");
        os << "pyc_xor #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(x.getResult()) << "_inst (\n";
        os << "  .a(" << nt.get(x.getLhs()) << "),\n";
        os << "  .b(" << nt.get(x.getRhs()) << "),\n";
        os << "  .y(" << nt.get(x.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto n = dyn_cast<pyc::NotOp>(op)) {
        auto yTy = dyn_cast<IntegerType>(n.getResult().getType());
        if (!yTy)
          return n.emitError("verilog emitter only supports integer not data type");
        os << "pyc_not #(.WIDTH(" << yTy.getWidth() << ")) " << nt.get(n.getResult()) << "_inst (\n";
        os << "  .a(" << nt.get(n.getIn()) << "),\n";
        os << "  .y(" << nt.get(n.getResult()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto r = dyn_cast<pyc::RegOp>(op)) {
        auto qTy = dyn_cast<IntegerType>(r.getQ().getType());
        if (!qTy)
          return r.emitError("verilog emitter only supports integer reg data type");
        os << "pyc_reg #(.WIDTH(" << qTy.getWidth() << ")) " << nt.get(r.getQ()) << "_inst (\n";
        os << "  .clk(" << nt.get(r.getClk()) << "),\n";
        os << "  .rst(" << nt.get(r.getRst()) << "),\n";
        os << "  .en(" << nt.get(r.getEn()) << "),\n";
        os << "  .d(" << nt.get(r.getNext()) << "),\n";
        os << "  .init(" << nt.get(r.getInit()) << "),\n";
        os << "  .q(" << nt.get(r.getQ()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto fifo = dyn_cast<pyc::FifoOp>(op)) {
        auto inDataTy = dyn_cast<IntegerType>(fifo.getInData().getType());
        if (!inDataTy)
          return fifo.emitError("verilog emitter only supports integer fifo data type");
        auto depth = fifo->getAttrOfType<IntegerAttr>("depth").getValue().getZExtValue();
        os << "pyc_fifo #(.WIDTH(" << inDataTy.getWidth() << "), .DEPTH(" << depth << ")) "
           << nt.get(fifo.getInReady()) << "_inst (\n";
        os << "  .clk(" << nt.get(fifo.getClk()) << "),\n";
        os << "  .rst(" << nt.get(fifo.getRst()) << "),\n";
        os << "  .in_valid(" << nt.get(fifo.getInValid()) << "),\n";
        os << "  .in_ready(" << nt.get(fifo.getInReady()) << "),\n";
        os << "  .in_data(" << nt.get(fifo.getInData()) << "),\n";
        os << "  .out_valid(" << nt.get(fifo.getOutValid()) << "),\n";
        os << "  .out_ready(" << nt.get(fifo.getOutReady()) << "),\n";
        os << "  .out_data(" << nt.get(fifo.getOutData()) << ")\n";
        os << ");\n";
        continue;
      }
      if (auto mem = dyn_cast<pyc::ByteMemOp>(op)) {
        auto addrTy = dyn_cast<IntegerType>(mem.getRaddr().getType());
        auto dataTy = dyn_cast<IntegerType>(mem.getRdata().getType());
        if (!addrTy || !dataTy)
          return mem.emitError("verilog emitter only supports integer byte_mem types");

        auto depthAttr = mem->getAttrOfType<IntegerAttr>("depth");
        if (!depthAttr)
          return mem.emitError("missing integer attribute `depth`");
        auto depth = depthAttr.getValue().getZExtValue();

        std::string inst = nt.get(mem.getRdata()) + "_inst";
        if (auto nameAttr = mem->getAttrOfType<StringAttr>("name"))
          inst = nameAttr.getValue().str();

        os << "pyc_byte_mem #(.ADDR_WIDTH(" << addrTy.getWidth() << "), .DATA_WIDTH(" << dataTy.getWidth() << "), .DEPTH("
           << depth << ")) " << inst << " (\n";
        os << "  .clk(" << nt.get(mem.getClk()) << "),\n";
        os << "  .rst(" << nt.get(mem.getRst()) << "),\n";
        os << "  .raddr(" << nt.get(mem.getRaddr()) << "),\n";
        os << "  .rdata(" << nt.get(mem.getRdata()) << "),\n";
        os << "  .wvalid(" << nt.get(mem.getWvalid()) << "),\n";
        os << "  .waddr(" << nt.get(mem.getWaddr()) << "),\n";
        os << "  .wdata(" << nt.get(mem.getWdata()) << "),\n";
        os << "  .wstrb(" << nt.get(mem.getWstrb()) << ")\n";
        os << ");\n";
        continue;
      }

      return op.emitError("unsupported op for verilog emission: ") << op.getName();
    }
  }

  // Connect outputs from return.
  auto ret = dyn_cast_or_null<func::ReturnOp>(f.getBody().front().getTerminator());
  if (!ret)
    return f.emitError("missing return");
  for (auto [i, v] : llvm::enumerate(ret.getOperands())) {
    os << "assign " << getPortName(f, i, /*isResult=*/true) << " = " << nt.get(v) << ";\n";
  }

  os << "\nendmodule\n\n";
  return success();
}

} // namespace

LogicalResult emitVerilog(ModuleOp module, llvm::raw_ostream &os, const VerilogEmitterOptions &opts) {
  if (opts.includePrimitives) {
    os << "`include \"pyc_add.sv\"\n";
    os << "`include \"pyc_mux.sv\"\n";
    os << "`include \"pyc_and.sv\"\n";
    os << "`include \"pyc_or.sv\"\n";
    os << "`include \"pyc_xor.sv\"\n";
    os << "`include \"pyc_not.sv\"\n";
    os << "`include \"pyc_reg.sv\"\n";
    os << "`include \"pyc_fifo.sv\"\n\n";
    os << "`include \"pyc_byte_mem.sv\"\n\n";
  }

  // Emit all functions as SystemVerilog modules (prototype convention).
  for (auto f : module.getOps<func::FuncOp>()) {
    if (failed(emitFunc(f, os, opts)))
      return failure();
  }
  return success();
}

} // namespace pyc
