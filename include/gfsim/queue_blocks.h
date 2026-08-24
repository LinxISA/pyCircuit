#ifndef GFSIM_QUEUE_BLOCKS_H
#define GFSIM_QUEUE_BLOCKS_H

#include "gfsim/object.h"
#include "gfsim/queue.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace gfsim {

template <typename Input, typename Output, typename Policy>
  requires std::invocable<const Policy &, const Input &> &&
           std::convertible_to<
               std::invoke_result_t<const Policy &, const Input &>, Output>
class QueueTransform final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.transform";
  static constexpr ObjectKind componentKind = ObjectKind::Compute;

  QueueTransform(std::string name, ObjectId id, SimObject *parent,
                 SimQueue<Input> &input, SimQueue<Output> &output,
                 Policy policy = {}, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), output_(output), policy_(std::move(policy)) {}

  void doWork(Epoch) override {
    if (fired_ || !input_.canProposePop() || !output_.canProposePush())
      return;
    const Input *head = input_.peek();
    if (head == nullptr)
      return;
    Output result = std::invoke(std::as_const(policy_), *head);
    if (!output_.proposePush(std::move(result)))
      return;
    if (!input_.proposePop())
      return;
    fired_ = true;
  }

  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && input_.canProposePop() && output_.canProposePush();
  }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<Input> &input_;
  SimQueue<Output> &output_;
  [[no_unique_address]] Policy policy_;
  bool fired_ = false;
};

template <typename Policy, typename InputTypes, typename OutputTypes>
class QueueAtomicTransform;

template <typename Policy, typename... Inputs, typename... Outputs>
  requires std::invocable<const Policy &, const Inputs &...> &&
           std::same_as<std::invoke_result_t<const Policy &, const Inputs &...>,
                        std::tuple<Outputs...>>
class QueueAtomicTransform<Policy, std::tuple<Inputs...>,
                           std::tuple<Outputs...>>
    final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.transform.atomic";
  static constexpr ObjectKind componentKind = ObjectKind::Compute;

  QueueAtomicTransform(std::string name, ObjectId id, SimObject *parent,
                       std::tuple<SimQueue<Inputs> *...> inputs,
                       std::tuple<SimQueue<Outputs> *...> outputs,
                       Policy policy = {},
                       ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        inputs_(inputs), outputs_(outputs), policy_(std::move(policy)) {}

  void doWork(Epoch) override {
    if (fired_ || !allInputsReady() || !allOutputsReady())
      return;
    auto values = inputValues(std::index_sequence_for<Inputs...>{});
    auto results = std::apply(std::as_const(policy_), values);
    if (!pushAll(results, std::index_sequence_for<Outputs...>{}) ||
        !popAll(std::index_sequence_for<Inputs...>{}))
      return;
    fired_ = true;
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && allInputsReady() && allOutputsReady();
  }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  bool allInputsReady() const {
    return std::apply(
        [](const auto *...queues) {
          return ((queues != nullptr && queues->canProposePop()) && ...);
        },
        inputs_);
  }
  bool allOutputsReady() const {
    return std::apply(
        [](const auto *...queues) {
          return ((queues != nullptr && queues->canProposePush()) && ...);
        },
        outputs_);
  }
  template <size_t... Indices>
  std::tuple<Inputs...> inputValues(std::index_sequence<Indices...>) const {
    return std::tuple<Inputs...>{*std::get<Indices>(inputs_)->peek()...};
  }
  template <size_t... Indices>
  bool pushAll(const std::tuple<Outputs...> &values,
               std::index_sequence<Indices...>) {
    return (
        std::get<Indices>(outputs_)->proposePush(std::get<Indices>(values)) &&
        ...);
  }
  template <size_t... Indices> bool popAll(std::index_sequence<Indices...>) {
    return (std::get<Indices>(inputs_)->proposePop().has_value() && ...);
  }

  std::tuple<SimQueue<Inputs> *...> inputs_;
  std::tuple<SimQueue<Outputs> *...> outputs_;
  [[no_unique_address]] Policy policy_;
  bool fired_ = false;
};

template <typename Types> class QueueBarrier;

template <typename... Values>
class QueueBarrier<std::tuple<Values...>> final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.barrier";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  QueueBarrier(std::string name, ObjectId id, SimObject *parent,
               std::tuple<SimQueue<Values> *...> inputs,
               std::tuple<SimQueue<Values> *...> outputs,
               ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        inputs_(inputs), outputs_(outputs) {}

  void doWork(Epoch) override {
    if (fired_ || !allInputsReady() || !allOutputsReady())
      return;
    auto values = inputValues(std::index_sequence_for<Values...>{});
    if (!pushAll(values, std::index_sequence_for<Values...>{}) ||
        !popAll(std::index_sequence_for<Values...>{}))
      return;
    fired_ = true;
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && allInputsReady() && allOutputsReady();
  }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  bool allInputsReady() const {
    return std::apply(
        [](const auto *...queues) {
          return ((queues != nullptr && queues->canProposePop()) && ...);
        },
        inputs_);
  }
  bool allOutputsReady() const {
    return std::apply(
        [](const auto *...queues) {
          return ((queues != nullptr && queues->canProposePush()) && ...);
        },
        outputs_);
  }
  template <size_t... Indices>
  std::tuple<Values...> inputValues(std::index_sequence<Indices...>) const {
    return std::tuple<Values...>{*std::get<Indices>(inputs_)->peek()...};
  }
  template <size_t... Indices>
  bool pushAll(const std::tuple<Values...> &values,
               std::index_sequence<Indices...>) {
    return (
        std::get<Indices>(outputs_)->proposePush(std::get<Indices>(values)) &&
        ...);
  }
  template <size_t... Indices> bool popAll(std::index_sequence<Indices...>) {
    return (std::get<Indices>(inputs_)->proposePop().has_value() && ...);
  }

  std::tuple<SimQueue<Values> *...> inputs_;
  std::tuple<SimQueue<Values> *...> outputs_;
  bool fired_ = false;
};

template <typename T, typename Key>
  requires std::invocable<const Key &, const T &> &&
           std::integral<std::invoke_result_t<const Key &, const T &>>
class QueueReorder final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.reorder";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  QueueReorder(std::string name, ObjectId id, SimObject *parent,
               SimQueue<T> &input, SimQueue<T> &output, size_t capacity,
               uint64_t start = 0, Key key = {},
               ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), output_(output), capacity_(capacity), start_(start),
        nextKey_(start), key_(std::move(key)) {}

  void doWork(Epoch) override {
    if (!pendingInput_ && entries_.size() < capacity_ &&
        input_.canProposePop()) {
      const T *head = input_.peek();
      if (head != nullptr) {
        using KeyResult = std::invoke_result_t<const Key &, const T &>;
        const KeyResult rawKey = std::invoke(std::as_const(key_), *head);
        if constexpr (std::signed_integral<KeyResult>)
          if (rawKey < 0) {
            setRuntimeFailureCode("reorder_negative_key");
            return;
          }
        const uint64_t key = static_cast<uint64_t>(rawKey);
        if (key < nextKey_) {
          setRuntimeFailureCode("reorder_stale_key");
          return;
        }
        if (entries_.contains(key)) {
          setRuntimeFailureCode("reorder_duplicate_key");
          return;
        }
        if (input_.proposePop())
          pendingInput_ = std::pair<uint64_t, T>{key, *head};
      }
    }
    if (!pendingOutputKey_) {
      auto next = entries_.find(nextKey_);
      if (next != entries_.end() && output_.canProposePush() &&
          output_.proposePush(next->second))
        pendingOutputKey_ = nextKey_;
    }
  }

  void doXfer(Epoch) override {
    if (pendingOutputKey_) {
      entries_.erase(*pendingOutputKey_);
      ++nextKey_;
      pendingOutputKey_.reset();
    }
    if (pendingInput_) {
      entries_.emplace(pendingInput_->first, std::move(pendingInput_->second));
      pendingInput_.reset();
    }
  }
  bool hasPendingCommit() const override {
    return pendingInput_.has_value() || pendingOutputKey_.has_value();
  }
  bool isRunnable(Epoch) const override {
    const bool canRetire = !pendingOutputKey_ && entries_.contains(nextKey_) &&
                           output_.canProposePush();
    const bool canAdmit =
        !pendingInput_ && entries_.size() < capacity_ && input_.canProposePop();
    return canRetire || canAdmit;
  }
  size_t buffered() const { return entries_.size(); }
  uint64_t nextKey() const { return nextKey_; }
  void reset() override {
    entries_.clear();
    pendingInput_.reset();
    pendingOutputKey_.reset();
    nextKey_ = start_;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  SimQueue<T> &output_;
  size_t capacity_;
  uint64_t start_;
  uint64_t nextKey_;
  [[no_unique_address]] Key key_;
  std::map<uint64_t, T> entries_;
  std::optional<std::pair<uint64_t, T>> pendingInput_;
  std::optional<uint64_t> pendingOutputKey_;
};

template <typename T, typename Key, typename Dependency, typename Resource,
          typename Cost>
  requires std::invocable<const Key &, const T &> &&
           std::integral<std::invoke_result_t<const Key &, const T &>> &&
           std::invocable<const Dependency &, const T &> &&
           std::integral<std::invoke_result_t<const Dependency &, const T &>> &&
           std::invocable<const Resource &, const T &> &&
           std::integral<std::invoke_result_t<const Resource &, const T &>> &&
           std::invocable<const Cost &, const T &> &&
           std::integral<std::invoke_result_t<const Cost &, const T &>>
class QueueDependency final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.dependency";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  QueueDependency(std::string name, ObjectId id, SimObject *parent,
                  SimQueue<T> &input, SimQueue<T> &output, size_t capacity,
                  size_t resources, uint64_t noDependency, Key key = {},
                  Dependency dependency = {}, Resource resource = {},
                  Cost cost = {}, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), output_(output), capacity_(capacity),
        resources_(resources), noDependency_(noDependency),
        key_(std::move(key)), dependency_(std::move(dependency)),
        resource_(std::move(resource)), cost_(std::move(cost)) {}

  void doWork(Epoch epoch) override {
    if (proposed_)
      return;
    if (entries_.size() < capacity_ && input_.canProposePop()) {
      const T *head = input_.peek();
      if (head != nullptr && !proposeInput(*head))
        return;
    }

    const Entry *completed = nullptr;
    for (const auto &[key, entry] : entries_)
      if (entry.state == State::Done &&
          (completed == nullptr || entry.ready < completed->ready ||
           (entry.ready == completed->ready && key < completed->key)))
        completed = &entry;
    if (completed != nullptr && output_.canProposePush() &&
        output_.proposePush(completed->value))
      pendingOutputKey_ = completed->key;

    for (const auto &[key, entry] : entries_)
      if (entry.state == State::Executing && entry.ready <= epoch)
        pendingCompletions_.push_back(key);
    for (size_t resource = 0; resource < resources_; ++resource) {
      if (!resourceFree(resource, epoch))
        continue;
      for (const auto &[key, entry] : entries_)
        if (entry.state == State::Waiting && entry.resource == resource &&
            dependencyReady(entry)) {
          pendingIssues_.push_back(key);
          break;
        }
    }
    proposed_ = pendingInput_.has_value() || pendingOutputKey_.has_value() ||
                !pendingIssues_.empty() || !pendingCompletions_.empty();
  }

  void doXfer(Epoch epoch) override {
    if (!proposed_)
      return;
    if (pendingOutputKey_)
      entries_.erase(*pendingOutputKey_);
    for (uint64_t key : pendingCompletions_)
      if (auto found = entries_.find(key); found != entries_.end())
        found->second.state = State::Done;
    for (uint64_t key : pendingIssues_)
      if (auto found = entries_.find(key); found != entries_.end()) {
        if (epoch.time >
            std::numeric_limits<uint64_t>::max() - found->second.cost) {
          setRuntimeFailureCode("dependency_time_overflow");
          continue;
        }
        found->second.state = State::Executing;
        found->second.ready = {epoch.time + found->second.cost, 0};
      }
    if (pendingInput_) {
      Entry entry;
      entry.key = pendingInput_->key;
      entry.dependency = pendingInput_->dependency;
      entry.resource = pendingInput_->resource;
      entry.cost = pendingInput_->cost;
      entry.value = std::move(pendingInput_->value);
      entries_.emplace(entry.key, std::move(entry));
    }
    pendingInput_.reset();
    pendingOutputKey_.reset();
    pendingIssues_.clear();
    pendingCompletions_.clear();
    proposed_ = false;
  }
  bool hasPendingCommit() const override { return proposed_; }
  bool isRunnable(Epoch epoch) const override {
    if (proposed_)
      return false;
    if (entries_.size() < capacity_ && input_.canProposePop())
      return true;
    for (const auto &[key, entry] : entries_) {
      (void)key;
      if ((entry.state == State::Done && output_.canProposePush()) ||
          (entry.state == State::Executing && entry.ready <= epoch) ||
          (entry.state == State::Waiting && dependencyReady(entry) &&
           resourceFree(entry.resource, epoch)))
        return true;
    }
    return false;
  }
  size_t active() const { return entries_.size(); }
  void reset() override {
    entries_.clear();
    pendingInput_.reset();
    pendingOutputKey_.reset();
    pendingIssues_.clear();
    pendingCompletions_.clear();
    proposed_ = false;
    clearRuntimeFailureCode();
  }

private:
  enum class State : uint8_t { Waiting, Executing, Done };
  struct Entry {
    uint64_t key = 0;
    uint64_t dependency = 0;
    uint64_t resource = 0;
    uint64_t cost = 0;
    T value{};
    State state = State::Waiting;
    Epoch ready{};
  };
  struct PendingInput {
    uint64_t key = 0;
    uint64_t dependency = 0;
    uint64_t resource = 0;
    uint64_t cost = 0;
    T value;
  };

  bool dependencyReady(const Entry &entry) const {
    if (entry.dependency == noDependency_)
      return true;
    auto found = entries_.find(entry.dependency);
    return found != entries_.end() && found->second.state == State::Done;
  }
  bool resourceFree(uint64_t resource, Epoch epoch) const {
    if (resource >= resources_)
      return false;
    for (const auto &[key, entry] : entries_) {
      (void)key;
      if (entry.resource == resource && entry.state == State::Executing &&
          entry.ready > epoch)
        return false;
    }
    return true;
  }
  bool proposeInput(const T &value) {
    using KeyResult = std::invoke_result_t<const Key &, const T &>;
    using DependencyResult =
        std::invoke_result_t<const Dependency &, const T &>;
    using ResourceResult = std::invoke_result_t<const Resource &, const T &>;
    using CostResult = std::invoke_result_t<const Cost &, const T &>;
    const KeyResult rawKey = std::invoke(std::as_const(key_), value);
    const DependencyResult rawDependency =
        std::invoke(std::as_const(dependency_), value);
    const ResourceResult rawResource =
        std::invoke(std::as_const(resource_), value);
    const CostResult rawCost = std::invoke(std::as_const(cost_), value);
    if constexpr (std::signed_integral<KeyResult>)
      if (rawKey < 0) {
        setRuntimeFailureCode("dependency_negative_key");
        return false;
      }
    if constexpr (std::signed_integral<DependencyResult>)
      if (rawDependency < 0) {
        setRuntimeFailureCode("dependency_negative_predecessor");
        return false;
      }
    if constexpr (std::signed_integral<ResourceResult>)
      if (rawResource < 0) {
        setRuntimeFailureCode("dependency_negative_resource");
        return false;
      }
    if constexpr (std::signed_integral<CostResult>)
      if (rawCost <= 0) {
        setRuntimeFailureCode("dependency_nonpositive_cost");
        return false;
      }
    if constexpr (std::unsigned_integral<CostResult>)
      if (rawCost == 0) {
        setRuntimeFailureCode("dependency_nonpositive_cost");
        return false;
      }
    const uint64_t key = static_cast<uint64_t>(rawKey);
    const uint64_t predecessor = static_cast<uint64_t>(rawDependency);
    const uint64_t resource = static_cast<uint64_t>(rawResource);
    const uint64_t cost = static_cast<uint64_t>(rawCost);
    if (entries_.contains(key)) {
      setRuntimeFailureCode("dependency_duplicate_key");
      return false;
    }
    if (resource >= resources_) {
      setRuntimeFailureCode("dependency_resource_out_of_range");
      return false;
    }
    if (!input_.proposePop())
      return true;
    pendingInput_ = PendingInput{key, predecessor, resource, cost, value};
    return true;
  }

  SimQueue<T> &input_;
  SimQueue<T> &output_;
  size_t capacity_;
  size_t resources_;
  uint64_t noDependency_;
  [[no_unique_address]] Key key_;
  [[no_unique_address]] Dependency dependency_;
  [[no_unique_address]] Resource resource_;
  [[no_unique_address]] Cost cost_;
  std::map<uint64_t, Entry> entries_;
  std::optional<PendingInput> pendingInput_;
  std::optional<uint64_t> pendingOutputKey_;
  std::vector<uint64_t> pendingIssues_;
  std::vector<uint64_t> pendingCompletions_;
  bool proposed_ = false;
};

template <typename T, typename Cost>
  requires std::invocable<const Cost &, const T &> &&
           std::integral<std::invoke_result_t<const Cost &, const T &>>
class QueueCredit final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.credit";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  QueueCredit(std::string name, ObjectId id, SimObject *parent,
              SimQueue<T> &input, SimQueue<T> &output, size_t credits,
              Cost cost = {}, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), output_(output), slots_(credits),
        cost_(std::move(cost)) {}

  void doWork(Epoch) override {
    if (proposed_)
      return;

    for (size_t index = 0; index < slots_.size(); ++index)
      if (slots_[index] && slots_[index]->remaining == 0 &&
          output_.canProposePush() &&
          output_.proposePush(slots_[index]->value)) {
        pendingOutput_ = index;
        break;
      }

    for (size_t index = 0; index < slots_.size(); ++index)
      if (slots_[index] && slots_[index]->remaining > 0)
        pendingCountdowns_.push_back(index);

    if (input_.canProposePop()) {
      auto free = std::find_if(slots_.begin(), slots_.end(),
                               [](const auto &slot) { return !slot; });
      const T *head = input_.peek();
      if (free != slots_.end() && head != nullptr)
        proposeInput(static_cast<size_t>(free - slots_.begin()), *head);
    }

    proposed_ = pendingInput_.has_value() || pendingOutput_.has_value() ||
                !pendingCountdowns_.empty();
  }

  void doXfer(Epoch) override {
    if (!proposed_)
      return;
    if (pendingOutput_)
      slots_[*pendingOutput_].reset();
    for (size_t index : pendingCountdowns_)
      if (slots_[index] && slots_[index]->remaining > 0)
        --slots_[index]->remaining;
    if (pendingInput_)
      slots_[pendingInput_->slot] =
          Entry{std::move(pendingInput_->value), pendingInput_->cost};
    pendingInput_.reset();
    pendingOutput_.reset();
    pendingCountdowns_.clear();
    proposed_ = false;
  }

  bool hasPendingCommit() const override { return proposed_; }
  bool isRunnable(Epoch) const override {
    if (proposed_)
      return false;
    if (input_.canProposePop() &&
        std::any_of(slots_.begin(), slots_.end(),
                    [](const auto &slot) { return !slot; }))
      return true;
    for (const auto &slot : slots_)
      if (slot && (slot->remaining > 0 || output_.canProposePush()))
        return true;
    return false;
  }

  size_t active() const {
    return static_cast<size_t>(
        std::count_if(slots_.begin(), slots_.end(),
                      [](const auto &slot) { return slot.has_value(); }));
  }

  void reset() override {
    for (auto &slot : slots_)
      slot.reset();
    pendingInput_.reset();
    pendingOutput_.reset();
    pendingCountdowns_.clear();
    proposed_ = false;
    clearRuntimeFailureCode();
  }

private:
  struct Entry {
    T value;
    uint64_t remaining = 0;
  };
  struct PendingInput {
    size_t slot = 0;
    T value;
    uint64_t cost = 0;
  };

  void proposeInput(size_t slot, const T &value) {
    using CostResult = std::invoke_result_t<const Cost &, const T &>;
    const CostResult rawCost = std::invoke(std::as_const(cost_), value);
    if constexpr (std::signed_integral<CostResult>)
      if (rawCost <= 0) {
        setRuntimeFailureCode("credit_nonpositive_cost");
        return;
      }
    if constexpr (std::unsigned_integral<CostResult>)
      if (rawCost == 0) {
        setRuntimeFailureCode("credit_nonpositive_cost");
        return;
      }
    if (!input_.proposePop())
      return;
    pendingInput_ = PendingInput{slot, value, static_cast<uint64_t>(rawCost)};
  }

  SimQueue<T> &input_;
  SimQueue<T> &output_;
  std::vector<std::optional<Entry>> slots_;
  [[no_unique_address]] Cost cost_;
  std::optional<PendingInput> pendingInput_;
  std::optional<size_t> pendingOutput_;
  std::vector<size_t> pendingCountdowns_;
  bool proposed_ = false;
};

template <typename T, typename Data, typename Address, typename Write,
          typename WriteData, typename Response>
  requires std::invocable<const Address &, const T &> &&
           std::integral<std::invoke_result_t<const Address &, const T &>> &&
           std::invocable<const Write &, const T &> &&
           std::convertible_to<std::invoke_result_t<const Write &, const T &>,
                               bool> &&
           std::invocable<const WriteData &, const T &> &&
           std::convertible_to<
               std::invoke_result_t<const WriteData &, const T &>, Data> &&
           std::invocable<const Response &, const T &, const Data &> &&
           std::convertible_to<
               std::invoke_result_t<const Response &, const T &, const Data &>,
               T>
class QueueMemory final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.memory";
  static constexpr ObjectKind componentKind = ObjectKind::Memory;

  QueueMemory(std::string name, ObjectId id, SimObject *parent,
              SimQueue<T> &input, SimQueue<T> &output, size_t entries,
              Data init = {}, Address address = {}, Write write = {},
              WriteData writeData = {}, Response response = {},
              ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), output_(output), init_(init), storage_(entries, init),
        address_(std::move(address)), write_(std::move(write)),
        writeData_(std::move(writeData)), response_(std::move(response)) {}

  void doWork(Epoch) override {
    if (fired_ || !input_.canProposePop() || !output_.canProposePush())
      return;
    const T *head = input_.peek();
    if (head == nullptr)
      return;
    using AddressResult = std::invoke_result_t<const Address &, const T &>;
    const AddressResult rawAddress =
        std::invoke(std::as_const(address_), *head);
    if constexpr (std::signed_integral<AddressResult>)
      if (rawAddress < 0) {
        setRuntimeFailureCode("memory_address_out_of_range");
        return;
      }
    const uint64_t address = static_cast<uint64_t>(rawAddress);
    if (address >= storage_.size()) {
      setRuntimeFailureCode("memory_address_out_of_range");
      return;
    }
    const Data oldData = storage_[address];
    T response = std::invoke(std::as_const(response_), *head, oldData);
    if (!output_.proposePush(std::move(response)) || !input_.proposePop())
      return;
    if (static_cast<bool>(std::invoke(std::as_const(write_), *head)))
      pendingWrite_ = std::pair<size_t, Data>{
          static_cast<size_t>(address),
          static_cast<Data>(std::invoke(std::as_const(writeData_), *head))};
    fired_ = true;
  }
  void doXfer(Epoch) override {
    if (pendingWrite_) {
      storage_[pendingWrite_->first] = std::move(pendingWrite_->second);
      pendingWrite_.reset();
    }
    fired_ = false;
  }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && input_.canProposePop() && output_.canProposePush();
  }
  const Data &at(size_t address) const { return storage_.at(address); }
  void reset() override {
    std::fill(storage_.begin(), storage_.end(), init_);
    pendingWrite_.reset();
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  SimQueue<T> &output_;
  Data init_;
  std::vector<Data> storage_;
  [[no_unique_address]] Address address_;
  [[no_unique_address]] Write write_;
  [[no_unique_address]] WriteData writeData_;
  [[no_unique_address]] Response response_;
  std::optional<std::pair<size_t, Data>> pendingWrite_;
  bool fired_ = false;
};

template <typename T> class QueueSink final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.sink";
  static constexpr ObjectKind componentKind = ObjectKind::Sink;

  QueueSink(std::string name, ObjectId id, SimObject *parent,
            SimQueue<T> &input, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input) {}

  void doWork(Epoch) override {
    if (pending_ || !input_.canProposePop())
      return;
    pending_ = input_.proposePop();
  }
  void doXfer(Epoch) override {
    if (pending_)
      received_.push_back(std::move(*pending_));
    pending_.reset();
  }
  bool hasPendingCommit() const override { return pending_.has_value(); }
  bool isRunnable(Epoch) const override {
    return !pending_ && input_.canProposePop();
  }
  const std::vector<T> &received() const { return received_; }
  void reset() override {
    pending_.reset();
    received_.clear();
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::optional<T> pending_;
  std::vector<T> received_;
};

template <typename T>
  requires std::equality_comparable<T>
class QueueObserve final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.observe";
  static constexpr ObjectKind componentKind = ObjectKind::Probe;

  QueueObserve(std::string name, ObjectId id, SimObject *parent,
               SimQueue<T> &input, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input) {}

  void doWork(Epoch) override {
    const T *head = input_.peek();
    if (pending_ || head == nullptr)
      return;
    if (last_ && input_.totalPops() == lastPopCount_ && *last_ == *head)
      return;
    pending_ = *head;
    pendingPopCount_ = input_.totalPops();
  }
  void doXfer(Epoch) override {
    if (!pending_)
      return;
    observed_.push_back(*pending_);
    last_ = std::move(pending_);
    pending_.reset();
    lastPopCount_ = pendingPopCount_;
  }
  bool hasPendingCommit() const override { return pending_.has_value(); }
  bool isRunnable(Epoch) const override {
    const T *head = input_.peek();
    return !pending_ && head != nullptr &&
           (!last_ || input_.totalPops() != lastPopCount_ || *last_ != *head);
  }
  const std::vector<T> &observed() const { return observed_; }
  void reset() override {
    pending_.reset();
    last_.reset();
    observed_.clear();
    lastPopCount_ = 0;
    pendingPopCount_ = 0;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::optional<T> pending_;
  std::optional<T> last_;
  std::vector<T> observed_;
  uint64_t lastPopCount_ = 0;
  uint64_t pendingPopCount_ = 0;
};

template <typename T, typename Predicate>
  requires std::equality_comparable<T> &&
           std::predicate<const Predicate &, const T &>
class QueueExpect final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.expect";
  static constexpr ObjectKind componentKind = ObjectKind::Probe;

  QueueExpect(std::string name, ObjectId id, SimObject *parent,
              SimQueue<T> &input, std::string message, Predicate predicate = {},
              ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), message_(std::move(message)),
        predicate_(std::move(predicate)) {}

  void doWork(Epoch) override {
    const T *head = input_.peek();
    if (pending_ || head == nullptr)
      return;
    if (last_ && input_.totalPops() == lastPopCount_ && *last_ == *head)
      return;
    if (!std::invoke(std::as_const(predicate_), *head)) {
      setRuntimeFailureCode("expectation_failed");
      return;
    }
    pending_ = *head;
    pendingPopCount_ = input_.totalPops();
  }
  void doXfer(Epoch) override {
    if (!pending_)
      return;
    last_ = std::move(pending_);
    pending_.reset();
    lastPopCount_ = pendingPopCount_;
  }
  bool hasPendingCommit() const override { return pending_.has_value(); }
  bool isRunnable(Epoch) const override {
    const T *head = input_.peek();
    return !pending_ && head != nullptr &&
           (!last_ || input_.totalPops() != lastPopCount_ || *last_ != *head);
  }
  std::string_view message() const { return message_; }
  void reset() override {
    pending_.reset();
    last_.reset();
    lastPopCount_ = 0;
    pendingPopCount_ = 0;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::string message_;
  [[no_unique_address]] Predicate predicate_;
  std::optional<T> pending_;
  std::optional<T> last_;
  uint64_t lastPopCount_ = 0;
  uint64_t pendingPopCount_ = 0;
};

template <typename T, size_t Outputs>
class QueueBroadcast final : public SimObject {
public:
  static_assert(Outputs >= 2);
  static constexpr std::string_view contractName = "ac.broadcast";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  QueueBroadcast(std::string name, ObjectId id, SimObject *parent,
                 SimQueue<T> &input, std::array<SimQueue<T> *, Outputs> outputs,
                 ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), outputs_(outputs) {}

  void doWork(Epoch) override {
    if (fired_ || !input_.canProposePop() ||
        std::any_of(outputs_.begin(), outputs_.end(), [](const auto *output) {
          return output == nullptr || !output->canProposePush();
        }))
      return;
    const T *head = input_.peek();
    if (head == nullptr)
      return;
    for (SimQueue<T> *output : outputs_)
      if (!output->proposePush(*head))
        return;
    if (!input_.proposePop())
      return;
    fired_ = true;
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && input_.canProposePop() &&
           std::all_of(outputs_.begin(), outputs_.end(),
                       [](const auto *output) {
                         return output != nullptr && output->canProposePush();
                       });
  }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::array<SimQueue<T> *, Outputs> outputs_;
  bool fired_ = false;
};

template <typename T, size_t Outputs> class QueueFork final : public SimObject {
public:
  static_assert(Outputs >= 2);
  static constexpr std::string_view contractName = "ac.fork";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  QueueFork(std::string name, ObjectId id, SimObject *parent,
            SimQueue<T> &input, std::array<SimQueue<T> *, Outputs> outputs,
            ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), outputs_(outputs) {}

  void doWork(Epoch) override {
    if (proposal_)
      return;
    const T *token = pending_ ? &*pending_ : input_.peek();
    if (token == nullptr)
      return;
    std::array<bool, Outputs> next = delivered_;
    bool changed = !pending_.has_value();
    for (size_t index = 0; index < Outputs; ++index) {
      SimQueue<T> *output = outputs_[index];
      if (next[index] || output == nullptr || !output->canProposePush())
        continue;
      if (!output->proposePush(*token))
        continue;
      next[index] = true;
      changed = true;
    }
    const bool deliveredAll =
        std::all_of(next.begin(), next.end(), [](bool value) { return value; });
    bool complete = false;
    if (deliveredAll && input_.canProposePop()) {
      complete = input_.proposePop().has_value();
      changed = changed || complete;
    }
    if (!changed)
      return;
    proposedToken_ = *token;
    proposedDelivered_ = next;
    proposedComplete_ = complete;
    proposal_ = true;
  }
  void doXfer(Epoch) override {
    if (!proposal_)
      return;
    if (proposedComplete_) {
      pending_.reset();
      delivered_.fill(false);
    } else {
      pending_ = std::move(proposedToken_);
      delivered_ = proposedDelivered_;
    }
    proposedToken_.reset();
    proposedDelivered_.fill(false);
    proposedComplete_ = false;
    proposal_ = false;
  }
  bool hasPendingCommit() const override { return proposal_; }
  bool isRunnable(Epoch) const override {
    if (proposal_ || (!pending_ && input_.peek() == nullptr))
      return false;
    for (size_t index = 0; index < Outputs; ++index)
      if (!delivered_[index] && outputs_[index] != nullptr &&
          outputs_[index]->canProposePush())
        return true;
    return false;
  }
  void reset() override {
    pending_.reset();
    proposedToken_.reset();
    delivered_.fill(false);
    proposedDelivered_.fill(false);
    proposedComplete_ = false;
    proposal_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::array<SimQueue<T> *, Outputs> outputs_;
  std::optional<T> pending_;
  std::optional<T> proposedToken_;
  std::array<bool, Outputs> delivered_{};
  std::array<bool, Outputs> proposedDelivered_{};
  bool proposedComplete_ = false;
  bool proposal_ = false;
};

template <typename T, size_t Outputs, typename Selector>
  requires std::invocable<const Selector &, const T &> &&
           std::integral<std::invoke_result_t<const Selector &, const T &>>
class QueueRoute final : public SimObject {
public:
  static_assert(Outputs >= 2);
  static constexpr std::string_view contractName = "ac.route";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  QueueRoute(std::string name, ObjectId id, SimObject *parent,
             SimQueue<T> &input, std::array<SimQueue<T> *, Outputs> outputs,
             Selector selector = {}, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), outputs_(outputs), selector_(std::move(selector)) {}

  void doWork(Epoch) override {
    if (fired_ || !input_.canProposePop())
      return;
    const T *head = input_.peek();
    if (head == nullptr)
      return;
    auto selected = std::invoke(std::as_const(selector_), *head);
    if constexpr (std::signed_integral<decltype(selected)>)
      if (selected < 0) {
        setRuntimeFailureCode("route_selector_out_of_range");
        return;
      }
    const size_t index = static_cast<size_t>(selected);
    if (index >= Outputs || outputs_[index] == nullptr) {
      setRuntimeFailureCode("route_selector_out_of_range");
      return;
    }
    if (!outputs_[index]->canProposePush())
      return;
    if (!outputs_[index]->proposePush(*head) || !input_.proposePop())
      return;
    fired_ = true;
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<T> &input_;
  std::array<SimQueue<T> *, Outputs> outputs_;
  [[no_unique_address]] Selector selector_;
  bool fired_ = false;
};

template <typename Control, typename T, size_t Inputs, typename Selector>
  requires std::invocable<const Selector &, const Control &> &&
           std::integral<
               std::invoke_result_t<const Selector &, const Control &>>
class QueueSelect final : public SimObject {
public:
  static_assert(Inputs >= 2);
  static constexpr std::string_view contractName = "ac.select";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  QueueSelect(std::string name, ObjectId id, SimObject *parent,
              SimQueue<Control> &control,
              std::array<SimQueue<T> *, Inputs> inputs, SimQueue<T> &output,
              Selector selector = {}, ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        control_(control), inputs_(inputs), output_(output),
        selector_(std::move(selector)) {}

  void doWork(Epoch) override {
    if (fired_ || !control_.canProposePop() || !output_.canProposePush())
      return;
    const Control *control = control_.peek();
    if (control == nullptr)
      return;
    auto selected = std::invoke(std::as_const(selector_), *control);
    if constexpr (std::signed_integral<decltype(selected)>)
      if (selected < 0) {
        setRuntimeFailureCode("select_selector_out_of_range");
        return;
      }
    const size_t index = static_cast<size_t>(selected);
    if (index >= Inputs || inputs_[index] == nullptr) {
      setRuntimeFailureCode("select_selector_out_of_range");
      return;
    }
    SimQueue<T> &input = *inputs_[index];
    const T *head = input.peek();
    if (!input.canProposePop() || head == nullptr)
      return;
    if (!output_.proposePush(*head) || !input.proposePop() ||
        !control_.proposePop())
      return;
    fired_ = true;
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  bool isRunnable(Epoch) const override {
    return !fired_ && control_.canProposePop() && output_.canProposePush();
  }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  SimQueue<Control> &control_;
  std::array<SimQueue<T> *, Inputs> inputs_;
  SimQueue<T> &output_;
  [[no_unique_address]] Selector selector_;
  bool fired_ = false;
};

enum class QueueMergePolicy { RoundRobin, Priority };

template <typename T, size_t Inputs> class QueueMerge final : public SimObject {
public:
  static_assert(Inputs >= 2);
  static constexpr std::string_view contractName = "ac.merge";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  QueueMerge(std::string name, ObjectId id, SimObject *parent,
             std::array<SimQueue<T> *, Inputs> inputs, SimQueue<T> &output,
             QueueMergePolicy policy = QueueMergePolicy::RoundRobin,
             ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        inputs_(inputs), output_(output), policy_(policy) {}

  void doWork(Epoch) override {
    if (selected_ || !output_.canProposePush())
      return;
    const size_t start =
        policy_ == QueueMergePolicy::RoundRobin ? cursor_ : size_t{0};
    for (size_t offset = 0; offset < Inputs; ++offset) {
      const size_t index = (start + offset) % Inputs;
      SimQueue<T> *input = inputs_[index];
      if (input == nullptr || !input->canProposePop())
        continue;
      const T *head = input->peek();
      if (head == nullptr || !output_.proposePush(*head) ||
          !input->proposePop())
        return;
      selected_ = index;
      return;
    }
  }
  void doXfer(Epoch) override {
    if (selected_ && policy_ == QueueMergePolicy::RoundRobin)
      cursor_ = (*selected_ + 1) % Inputs;
    selected_.reset();
  }
  bool hasPendingCommit() const override { return selected_.has_value(); }
  void reset() override {
    cursor_ = 0;
    selected_.reset();
    clearRuntimeFailureCode();
  }

private:
  std::array<SimQueue<T> *, Inputs> inputs_;
  SimQueue<T> &output_;
  QueueMergePolicy policy_;
  size_t cursor_ = 0;
  std::optional<size_t> selected_;
};

template <typename T> struct FeedbackToken {
  T value;
  size_t iteration = 0;
};

template <typename T, typename Update, typename Condition>
  requires std::invocable<const Update &, const T &> &&
           std::convertible_to<std::invoke_result_t<const Update &, const T &>,
                               T> &&
           std::predicate<const Condition &, const T &>
class QueueFeedback final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.feedback";
  static constexpr ObjectKind componentKind = ObjectKind::Compute;

  QueueFeedback(std::string name, ObjectId id, SimObject *parent,
                SimQueue<T> &input, SimQueue<FeedbackToken<T>> &feedback,
                SimQueue<T> &output, size_t maxIterations, Update update = {},
                Condition condition = {},
                ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        input_(input), feedback_(feedback), output_(output),
        maxIterations_(maxIterations), update_(std::move(update)),
        condition_(std::move(condition)) {}

  void doWork(Epoch) override {
    if (fired_)
      return;
    if (feedback_.canProposePop()) {
      const FeedbackToken<T> *head = feedback_.peek();
      if (head != nullptr)
        fire(*head, feedback_);
      return;
    }
    if (!input_.canProposePop())
      return;
    const T *head = input_.peek();
    if (head != nullptr)
      fire(FeedbackToken<T>{*head, 0}, input_);
  }
  void doXfer(Epoch) override { fired_ = false; }
  bool hasPendingCommit() const override { return fired_; }
  void reset() override {
    fired_ = false;
    clearRuntimeFailureCode();
  }

private:
  template <typename InputQueue>
  void fire(const FeedbackToken<T> &token, InputQueue &source) {
    if (!std::invoke(std::as_const(condition_), token.value)) {
      if (!output_.canProposePush() || !output_.proposePush(token.value) ||
          !source.proposePop())
        return;
      fired_ = true;
      return;
    }
    if (token.iteration >= maxIterations_) {
      setRuntimeFailureCode("feedback_iteration_limit");
      return;
    }
    const bool replacesFeedback =
        std::same_as<InputQueue, SimQueue<FeedbackToken<T>>>;
    const bool canPush = replacesFeedback ? feedback_.canProposePushAfterPop()
                                          : feedback_.canProposePush();
    if (!canPush)
      return;
    FeedbackToken<T> next{std::invoke(std::as_const(update_), token.value),
                          token.iteration + 1};
    if (!source.proposePop() || !feedback_.proposePush(std::move(next)))
      return;
    fired_ = true;
  }

  SimQueue<T> &input_;
  SimQueue<FeedbackToken<T>> &feedback_;
  SimQueue<T> &output_;
  size_t maxIterations_;
  [[no_unique_address]] Update update_;
  [[no_unique_address]] Condition condition_;
  bool fired_ = false;
};

} // namespace gfsim

#endif // GFSIM_QUEUE_BLOCKS_H
