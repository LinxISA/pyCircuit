#ifndef GFSIM_QUEUE_BLOCKS_H
#define GFSIM_QUEUE_BLOCKS_H

#include "gfsim/object.h"
#include "gfsim/queue.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
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

template <typename Policy, typename... Ts>
  requires std::invocable<const Policy &, const Ts &...> &&
           std::same_as<std::invoke_result_t<const Policy &, const Ts &...>,
                        std::tuple<Ts...>>
class QueueAtomicTransform final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.transform.atomic";
  static constexpr ObjectKind componentKind = ObjectKind::Compute;

  QueueAtomicTransform(std::string name, ObjectId id, SimObject *parent,
                       std::tuple<SimQueue<Ts> *...> inputs,
                       std::tuple<SimQueue<Ts> *...> outputs,
                       Policy policy = {},
                       ObservationSink *observations = nullptr)
      : SimObject(componentKind, std::move(name), id, parent, observations),
        inputs_(inputs), outputs_(outputs), policy_(std::move(policy)) {}

  void doWork(Epoch) override {
    if (fired_ || !allInputsReady() || !allOutputsReady())
      return;
    auto values = inputValues(std::index_sequence_for<Ts...>{});
    auto results = std::apply(std::as_const(policy_), values);
    if (!pushAll(results, std::index_sequence_for<Ts...>{}) ||
        !popAll(std::index_sequence_for<Ts...>{}))
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
  std::tuple<Ts...> inputValues(std::index_sequence<Indices...>) const {
    return std::tuple<Ts...>{*std::get<Indices>(inputs_)->peek()...};
  }
  template <size_t... Indices>
  bool pushAll(const std::tuple<Ts...> &values,
               std::index_sequence<Indices...>) {
    return (
        std::get<Indices>(outputs_)->proposePush(std::get<Indices>(values)) &&
        ...);
  }
  template <size_t... Indices> bool popAll(std::index_sequence<Indices...>) {
    return (std::get<Indices>(inputs_)->proposePop().has_value() && ...);
  }

  std::tuple<SimQueue<Ts> *...> inputs_;
  std::tuple<SimQueue<Ts> *...> outputs_;
  [[no_unique_address]] Policy policy_;
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
