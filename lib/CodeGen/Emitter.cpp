#include "acir/CodeGen/Emitter.h"

#include <sstream>
#include <algorithm>

namespace acir::codegen {

// ── CppEmitter ─────────────────────────────────────────────────────────

void CppEmitter::writeIndent() {
  for (int i = 0; i < indent_; ++i)
    os_ << "  ";
}

void CppEmitter::emitPragmaOnce() {
  os_ << "#pragma once\n\n";
  atLineStart_ = true;
}

void CppEmitter::emitInclude(const std::string &header, bool system) {
  if (system)
    os_ << "#include <" << header << ">\n";
  else
    os_ << "#include \"" << header << "\"\n";
  atLineStart_ = true;
}

void CppEmitter::emitNewline() {
  os_ << "\n";
  atLineStart_ = true;
}

void CppEmitter::emitComment(const std::string &text) {
  writeIndent();
  os_ << "// " << text << "\n";
  atLineStart_ = true;
}

void CppEmitter::beginNamespace(const std::string &name) {
  os_ << "namespace " << name << " {\n\n";
  atLineStart_ = true;
}

void CppEmitter::endNamespace() {
  os_ << "} // namespace\n\n";
  atLineStart_ = true;
}

void CppEmitter::beginClass(const std::string &name, const std::string &base,
                             bool isStruct) {
  writeIndent();
  os_ << (isStruct ? "struct " : "class ") << name;
  if (!base.empty())
    os_ << " : public " << base;
  os_ << " {\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::endClass() {
  dedent();
  writeIndent();
  os_ << "};\n\n";
  atLineStart_ = true;
}

void CppEmitter::emitPublic() {
  dedent();
  writeIndent();
  os_ << "public:\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitPrivate() {
  dedent();
  writeIndent();
  os_ << "private:\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitProtected() {
  dedent();
  writeIndent();
  os_ << "protected:\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitMember(const std::string &type, const std::string &name,
                              const std::string &init) {
  writeIndent();
  os_ << type << " " << name;
  if (!init.empty()) os_ << " = " << init;
  os_ << ";\n";
  atLineStart_ = true;
}

void CppEmitter::emitStaticMember(const std::string &type,
                                    const std::string &name,
                                    const std::string &init) {
  writeIndent();
  os_ << "static " << type << " " << name;
  if (!init.empty()) os_ << " = " << init;
  os_ << ";\n";
  atLineStart_ = true;
}

void CppEmitter::emitMethod(const std::string &returnType,
                              const std::string &name,
                              const std::vector<std::pair<std::string, std::string>> &params,
                              bool isConst, bool isOverride,
                              bool isVirtual, bool isStatic) {
  writeIndent();
  if (isStatic) os_ << "static ";
  if (isVirtual) os_ << "virtual ";
  os_ << returnType << " " << name << "(";
  for (size_t i = 0; i < params.size(); ++i) {
    os_ << params[i].first << " " << params[i].second;
    if (i + 1 < params.size()) os_ << ", ";
  }
  os_ << ")";
  if (isConst) os_ << " const";
  if (isOverride) os_ << " override";
  os_ << ";\n";
  atLineStart_ = true;
}

void CppEmitter::emitConstructor(
    const std::string &className,
    const std::vector<std::pair<std::string, std::string>> &params,
    const std::vector<std::string> &initializers, const std::string &body) {
  writeIndent();
  os_ << className << "(";
  for (size_t i = 0; i < params.size(); ++i) {
    os_ << params[i].first << " " << params[i].second;
    if (i + 1 < params.size()) os_ << ", ";
  }
  os_ << ")";
  if (!initializers.empty()) {
    os_ << "\n";
    writeIndent();
    for (size_t i = 0; i < initializers.size(); ++i) {
      os_ << "    : " << initializers[i];
      if (i + 1 < initializers.size()) {
        os_ << "\n";
        writeIndent();
      }
    }
  }
  if (!body.empty()) {
    os_ << " {\n";
    os_ << body;
    writeIndent();
    os_ << "}\n";
  } else {
    os_ << " = default;\n";
  }
  atLineStart_ = true;
}

void CppEmitter::beginMethodBody() {
  os_ << " {\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::endMethodBody() {
  dedent();
  writeIndent();
  os_ << "}\n\n";
  atLineStart_ = true;
}

void CppEmitter::emitReturn(const std::string &expr) {
  writeIndent();
  if (expr.empty())
    os_ << "return;\n";
  else
    os_ << "return " << expr << ";\n";
  atLineStart_ = true;
}

void CppEmitter::emitStatement(const std::string &stmt) {
  writeIndent();
  os_ << stmt << ";\n";
  atLineStart_ = true;
}

void CppEmitter::emitEnum(const std::string &name,
                            const std::vector<std::string> &values,
                            const std::string &underlyingType) {
  writeIndent();
  os_ << "enum class " << name << " : " << underlyingType << " {\n";
  indent();
  for (size_t i = 0; i < values.size(); ++i) {
    writeIndent();
    os_ << values[i];
    if (i + 1 < values.size()) os_ << ",";
    os_ << "\n";
  }
  dedent();
  writeIndent();
  os_ << "};\n\n";
  atLineStart_ = true;
}

void CppEmitter::emitSwitch(const std::string &expr) {
  writeIndent();
  os_ << "switch (" << expr << ") {\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitCase(const std::string &value) {
  dedent();
  writeIndent();
  os_ << "case " << value << ":\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitDefault() {
  dedent();
  writeIndent();
  os_ << "default:\n";
  indent();
  atLineStart_ = true;
}

void CppEmitter::emitBreak() {
  writeIndent();
  os_ << "break;\n";
  atLineStart_ = true;
}

void CppEmitter::endSwitch() {
  dedent();
  writeIndent();
  os_ << "}\n";
  atLineStart_ = true;
}

void CppEmitter::emitTemplate(const std::string &params) {
  writeIndent();
  os_ << "template <" << params << ">\n";
  atLineStart_ = true;
}

void CppEmitter::emitRaw(const std::string &code) {
  os_ << code;
  atLineStart_ = false;
}

void CppEmitter::indent() { ++indent_; }
void CppEmitter::dedent() {
  if (indent_ > 0) --indent_;
}

// ── Deterministic code generation ─────────────────────────────────────

static std::string className(const std::string &moduleName,
                             const std::string &processName) {
  return moduleName + "_" + processName;
}

SourceFile generateProcessHeader(
    const std::string &moduleName, const std::string &processName,
    const std::vector<std::string> &pcNames,
    const std::vector<std::string> &liveSlotTypes,
    const std::vector<std::string> &liveSlotNames) {

  std::ostringstream os;
  CppEmitter e(os);

  e.emitPragmaOnce();
  e.emitInclude("cstdint");
  e.emitInclude("gfsim/core.h", true);
  e.emitInclude("gfsim/object.h", true);
  e.emitNewline();

  std::string ns = "gfsim::generated::" + moduleName;
  e.beginNamespace(ns);

  std::string cls = className(moduleName, processName);
  e.beginClass(cls, "gfsim::SimObject");

  e.emitPublic();
  e.emitEnum("Pc", pcNames);
  e.emitConstructor(cls, {{"std::string", "name"}, {"gfsim::ObjectId", "id"},
                          {"gfsim::SimObject *", "parent"}},
                    {"SimObject(gfsim::ObjectKind::Process, std::move(name), "
                     "id, parent)",
                     "pc_(Pc::" + pcNames.front() + ")"});

  e.emitMethod("void", "doWork", {{"gfsim::Epoch", "epoch"}}, false, true);
  e.emitMethod("void", "doXfer", {{"gfsim::Epoch", "epoch"}}, false, true);
  e.emitMethod("bool", "isRunnable", {{"gfsim::Epoch", "epoch"}}, true, true);

  e.emitPrivate();
  e.emitMember("Pc", "pc_", "Pc::" + pcNames.front());
  for (size_t i = 0; i < liveSlotNames.size(); ++i)
    e.emitMember(liveSlotTypes[i], liveSlotNames[i]);

  e.endClass();
  e.endNamespace();

  std::string path = "include/generated/" + moduleName + "/" + cls + ".h";
  SourceFile sf;
  sf.relativePath = path;
  sf.content = os.str();
  sf.fingerprint = computeFingerprint(sf.content);
  return sf;
}

SourceFile generateProcessSource(
    const std::string &moduleName, const std::string &processName,
    const std::vector<std::string> &pcNames,
    const std::vector<std::string> &liveSlotTypes,
    const std::vector<std::string> &liveSlotNames) {

  std::ostringstream os;
  CppEmitter e(os);

  std::string cls = className(moduleName, processName);
  e.emitInclude("generated/" + moduleName + "/" + cls + ".h");
  e.emitNewline();

  std::string ns = "gfsim::generated::" + moduleName;
  e.beginNamespace(ns);

  // Constructor definition
  e.emitRaw(cls + "::" + cls +
            "(std::string name, gfsim::ObjectId id, gfsim::SimObject *parent)\n"
            "    : SimObject(gfsim::ObjectKind::Process, std::move(name), "
            "id, parent),\n"
            "      pc_(Pc::" + pcNames.front() + ") {}\n\n");

  // doWork
  e.emitRaw("void " + cls + "::doWork(gfsim::Epoch epoch) {\n");
  e.emitSwitch("pc_");
  for (size_t i = 0; i < pcNames.size(); ++i) {
    e.emitCase("Pc::" + pcNames[i]);
    e.emitStatement("// state machine logic for " + pcNames[i]);
    e.emitBreak();
  }
  e.emitDefault();
  e.emitBreak();
  e.endSwitch();
  e.emitRaw("}\n\n");

  // doXfer
  e.emitRaw("void " + cls + "::doXfer(gfsim::Epoch epoch) {\n");
  e.emitRaw("  // commit live state changes\n");
  e.emitRaw("}\n\n");

  // isRunnable
  e.emitRaw("bool " + cls + "::isRunnable(gfsim::Epoch) const {\n");
  e.emitRaw("  return true;\n");
  e.emitRaw("}\n\n");

  e.endNamespace();

  std::string path = "src/generated/" + moduleName + "/" + cls + ".cpp";
  SourceFile sf;
  sf.relativePath = path;
  sf.content = os.str();
  sf.fingerprint = computeFingerprint(sf.content);
  return sf;
}

SourceFile generateModuleHeader(
    const std::string &moduleName,
    const std::vector<std::string> &childNames) {

  std::ostringstream os;
  CppEmitter e(os);

  e.emitPragmaOnce();
  e.emitInclude("memory");
  e.emitInclude("gfsim/object.h", true);
  e.emitNewline();

  std::string ns = "gfsim::generated";
  e.beginNamespace(ns);

  std::string cls = moduleName + "Module";
  e.beginClass(cls, "gfsim::Module");
  e.emitPublic();
  e.emitConstructor(cls, {{"std::string", "name"}, {"gfsim::ObjectId", "id"},
                          {"gfsim::SimObject *", "parent"}},
                    {"Module(std::move(name), id, parent)"});
  e.emitMethod("void", "build", {}, false, false, false);

  e.emitPrivate();
  for (const auto &child : childNames)
    e.emitMember("std::unique_ptr<gfsim::SimObject>", child + "_");

  e.endClass();
  e.endNamespace();

  std::string path = "include/generated/" + cls + ".h";
  SourceFile sf;
  sf.relativePath = path;
  sf.content = os.str();
  sf.fingerprint = computeFingerprint(sf.content);
  return sf;
}

SourceFile generateModuleSource(
    const std::string &moduleName,
    const std::vector<std::string> &childNames) {

  std::ostringstream os;
  CppEmitter e(os);

  std::string cls = moduleName + "Module";
  e.emitInclude("generated/" + cls + ".h");
  e.emitNewline();

  std::string ns = "gfsim::generated";
  e.beginNamespace(ns);

  // constructor
  e.emitRaw(cls + "::" + cls +
            "(std::string name, gfsim::ObjectId id, gfsim::SimObject *parent)\n"
            "    : Module(std::move(name), id, parent) {}\n\n");

  // build
  e.emitRaw("void " + cls + "::build() {\n");
  for (const auto &child : childNames) {
    e.emitRaw("  " + child +
              "_ = std::make_unique<gfsim::SimObject>(\n"
              "      gfsim::ObjectKind::Compute, \"" +
              child + "\", 0, this);\n");
    e.emitRaw("  addChild(std::move(" + child + "_));\n");
  }
  e.emitRaw("}\n\n");

  e.endNamespace();

  std::string path = "src/generated/" + cls + ".cpp";
  SourceFile sf;
  sf.relativePath = path;
  sf.content = os.str();
  sf.fingerprint = computeFingerprint(sf.content);
  return sf;
}

} // namespace acir::codegen
