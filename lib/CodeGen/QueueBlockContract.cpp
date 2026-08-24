#include "acir/CodeGen/QueueBlockContract.h"

#include "acir/Bindings/Binding.h"

#include "llvm/Support/JSON.h"

#include <algorithm>

namespace acir::codegen {
namespace {

const std::vector<QueueBlockContract> Contracts = {
    {"dependency",
     "ac.dependency",
     "scheduling",
     "design",
     "stateful",
     1,
     1,
     1,
     1,
     "input_output_payload_equal",
     {"capacity", "resources", "no_dependency", "depth", "latency"},
     true,
     "gfsim::QueueDependency<T,Key,Dependency,Resource,Cost>",
     true,
     "packed_dependency_window_resource_reservation_and_countdown",
     {"accepted_transaction", "issued_transaction", "completed_transaction"}},
    {"source",
     "ac.source",
     "boundary",
     "design",
     "stateful",
     0,
     0,
     1,
     1,
     "declared_output_payload",
     {"depth", "latency"},
     true,
     "typed_source_boundary",
     true,
     "ready_valid_input_boundary",
     {"input_transaction"}},
    {"sink",
     "ac.sink",
     "boundary",
     "design",
     "stateful",
     1,
     1,
     0,
     0,
     "consume_input_payload",
     {},
     true,
     "gfsim::QueueSink<T>",
     true,
     "ready_valid_output_boundary",
     {"output_transaction"}},
    {"observe",
     "ac.observe",
     "boundary",
     "observation",
     "observation",
     1,
     1,
     0,
     0,
     "observe_input_payload",
     {"name"},
     true,
     "gfsim::QueueObserve<T>",
     true,
     "non_backpressuring_alias_probe",
     {"probe_value"}},
    {"transform",
     "ac.transform",
     "combinational",
     "design",
     "atomic",
     1,
     -1,
     1,
     -1,
     "region_yields_match_output_payloads",
     {"output_depths", "output_latencies"},
     true,
     "gfsim::QueueTransform/QueueAtomicTransform",
     true,
     "pure_logic_plus_ready_valid_fifos",
     {"accepted_transaction", "output_transaction"}},
    {"broadcast",
     "ac.broadcast",
     "transport",
     "design",
     "atomic",
     1,
     1,
     2,
     -1,
     "all_payloads_equal",
     {"output_depths", "output_latencies"},
     true,
     "gfsim::QueueBroadcast<T,N>",
     true,
     "all_output_ready_conjunction",
     {"input_transaction", "output_transactions"}},
    {"fork",
     "ac.fork",
     "transport",
     "design",
     "stateful",
     1,
     1,
     2,
     -1,
     "all_payloads_equal",
     {"output_depths", "output_latencies"},
     true,
     "gfsim::QueueFork<T,N>",
     true,
     "per_output_delivered_registers",
     {"input_transaction", "output_transactions"}},
    {"route",
     "ac.route",
     "transport",
     "design",
     "atomic",
     1,
     1,
     2,
     -1,
     "all_payloads_equal",
     {"output_depths", "output_latencies"},
     true,
     "gfsim::QueueRoute<T,N,Selector>",
     true,
     "selector_decoder_and_ready_mux",
     {"input_transaction", "selected_output_transaction"}},
    {"merge",
     "ac.merge",
     "transport",
     "design",
     "stateful",
     2,
     -1,
     1,
     1,
     "all_payloads_equal",
     {"policy", "depth", "latency"},
     true,
     "gfsim::QueueMerge<T,N>",
     true,
     "priority_or_round_robin_arbiter",
     {"selected_input_transaction", "output_transaction"}},
    {"reorder",
     "ac.reorder",
     "scheduling",
     "design",
     "stateful",
     1,
     1,
     1,
     1,
     "input_output_payload_equal",
     {"capacity", "start", "depth", "latency"},
     true,
     "gfsim::QueueReorder<T,Key>",
     true,
     "tagged_register_bank_and_expected_key",
     {"accepted_transaction", "retired_transaction", "sequence_order"}},
    {"feedback",
     "ac.feedback",
     "state",
     "design",
     "stateful",
     1,
     1,
     1,
     1,
     "input_output_payload_equal",
     {"depth", "latency", "max_iterations"},
     true,
     "gfsim::QueueFeedback<T,Update,Condition>",
     true,
     "valid_data_iteration_registers",
     {"input_transaction", "output_transaction", "iteration_limit"}},
    {"scope",
     "ac.scope",
     "structural",
     "design",
     "structural",
     0,
     -1,
     0,
     -1,
     "positionally_equal_boundary_payloads",
     {"sym_name"},
     true,
     "gfsim::Module_hierarchy",
     true,
     "elaboration_time_scope_flattening",
     {"boundary_transactions"}},
};

llvm::json::Object arity(int64_t minimum, int64_t maximum) {
  llvm::json::Object result{{"min", minimum}};
  if (maximum < 0)
    result["max"] = nullptr;
  else
    result["max"] = maximum;
  return result;
}

llvm::json::Array strings(llvm::ArrayRef<std::string> values) {
  llvm::json::Array result;
  for (const std::string &value : values)
    result.push_back(value);
  return result;
}

} // namespace

llvm::ArrayRef<QueueBlockContract> officialQueueBlockContracts() {
  return Contracts;
}

const QueueBlockContract *findQueueBlockContract(llvm::StringRef kind) {
  auto found = std::find_if(Contracts.begin(), Contracts.end(),
                            [&](const QueueBlockContract &contract) {
                              return contract.kind == kind;
                            });
  return found == Contracts.end() ? nullptr : &*found;
}

llvm::Expected<std::string> canonicalQueueBlockCatalogJson() {
  std::vector<const QueueBlockContract *> ordered;
  ordered.reserve(Contracts.size());
  for (const QueueBlockContract &contract : Contracts)
    ordered.push_back(&contract);
  std::sort(ordered.begin(), ordered.end(),
            [](const auto *left, const auto *right) {
              return left->operation < right->operation;
            });
  llvm::json::Array entries;
  for (const QueueBlockContract *contract : ordered) {
    entries.push_back(llvm::json::Object{
        {"category", contract->category},
        {"constants", strings(contract->constants)},
        {"effect", contract->effect},
        {"gfsim",
         llvm::json::Object{{"available", contract->gfsimAvailable},
                            {"realization", contract->gfsimRealization}}},
        {"inputs", arity(contract->minimumInputs, contract->maximumInputs)},
        {"kind", contract->kind},
        {"operation", contract->operation},
        {"outputs", arity(contract->minimumOutputs, contract->maximumOutputs)},
        {"payload_relation", contract->payloadRelation},
        {"pyc", llvm::json::Object{{"available", contract->pycAvailable},
                                   {"realization", contract->pycRealization}}},
        {"refinement_observations", strings(contract->refinementObservations)},
        {"role", contract->role},
    });
  }
  llvm::json::Object root{{"contract_epoch", "0.2"},
                          {"entries", std::move(entries)},
                          {"schema", "agentic-circuit-opcode-catalog"},
                          {"version", "0.2"}};
  return bindings::canonicalizeJson(llvm::json::Value(std::move(root)));
}

} // namespace acir::codegen
