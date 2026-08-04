#ifndef ACIR_LIB_BINDINGS_BINDINGTESTHOOKS_H
#define ACIR_LIB_BINDINGS_BINDINGTESTHOOKS_H

namespace acir::bindings::detail {

inline thread_local bool failBindingPublish = false;

class ScopedBindingPublishFailure {
public:
  ScopedBindingPublishFailure() : previous(failBindingPublish) {
    failBindingPublish = true;
  }

  ~ScopedBindingPublishFailure() { failBindingPublish = previous; }

  ScopedBindingPublishFailure(const ScopedBindingPublishFailure &) = delete;
  ScopedBindingPublishFailure &
  operator=(const ScopedBindingPublishFailure &) = delete;

private:
  bool previous;
};

inline bool shouldFailBindingPublish() { return failBindingPublish; }

} // namespace acir::bindings::detail

#endif // ACIR_LIB_BINDINGS_BINDINGTESTHOOKS_H
