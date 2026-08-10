#ifndef GFSIM_DISPATCH_H
#define GFSIM_DISPATCH_H

#include "gfsim/core.h"

#include <concepts>
#include <cstddef>
#include <span>

namespace gfsim {

class SimObject;

/// The two invocations of the generated Xfer thunk preserve the global
/// arbitration/Xfer barrier without adding a second dispatch entry point.
enum class XferPhase : uint8_t { Arbitrate, Commit };

using WorkThunk = void (*)(void *, Epoch);
using XferThunk = void (*)(void *, Epoch, XferPhase);
using ResetThunk = void (*)(void *);
using ValidateThunk = bool (*)(const void *, ObjectId, ObjectKind);

/// One generated row per runtime object. Rows are indexed by their dense,
/// stable ObjectId; the object pointer is recovered only by typed thunks.
struct DispatchRow {
  ObjectId id = kInvalidObjectId;
  ObjectKind kind = ObjectKind::Module;
  void *object = nullptr;
  WorkThunk work = nullptr;
  XferThunk xfer = nullptr;
  ResetThunk reset = nullptr;
  ValidateThunk validate = nullptr;
};

template <typename T>
concept DispatchObject =
    std::derived_from<T, SimObject> &&
    requires(T &object, const T &constObject, Epoch epoch) {
      { constObject.id() } -> std::convertible_to<ObjectId>;
      { constObject.kind() } -> std::same_as<ObjectKind>;
      object.doWork(epoch);
      object.doArbitrate(epoch);
      object.doXfer(epoch);
      object.reset();
    };

template <DispatchObject T> DispatchRow makeDispatchRow(T *object) {
  return DispatchRow{
      .id = object ? object->id() : kInvalidObjectId,
      .kind = object ? object->kind() : ObjectKind::Module,
      .object = static_cast<SimObject *>(object),
      .work =
          [](void *storage, Epoch epoch) {
            static_cast<T *>(static_cast<SimObject *>(storage))
                ->T::doWork(epoch);
          },
      .xfer =
          [](void *storage, Epoch epoch, XferPhase phase) {
            T *typed = static_cast<T *>(static_cast<SimObject *>(storage));
            if (phase == XferPhase::Arbitrate)
              typed->T::doArbitrate(epoch);
            else
              typed->T::doXfer(epoch);
          },
      .reset =
          [](void *storage) {
            static_cast<T *>(static_cast<SimObject *>(storage))->T::reset();
          },
      .validate =
          [](const void *storage, ObjectId id, ObjectKind kind) {
            const T *typed =
                static_cast<const T *>(static_cast<const SimObject *>(storage));
            if (typed->id() != id || typed->kind() != kind)
              return false;
            if constexpr (requires(const T &typed) {
                            { typed.validate() } -> std::convertible_to<bool>;
                          })
              return typed->T::validate();
            return true;
          },
  };
}

/// Non-owning view of the generated static table. Generated storage has static
/// lifetime; tests may provide an array whose lifetime encloses the system.
class DispatchTable {
public:
  DispatchTable() = default;
  explicit DispatchTable(std::span<const DispatchRow> rows) : rows_(rows) {}

  size_t size() const { return rows_.size(); }
  bool empty() const { return rows_.empty(); }

  const DispatchRow *lookup(ObjectId id) const {
    if (id >= rows_.size())
      return nullptr;
    return &rows_[id];
  }

  bool validate() const {
    for (size_t index = 0; index < rows_.size(); ++index) {
      const DispatchRow &row = rows_[index];
      if (row.id != index || !row.object || !row.work || !row.xfer ||
          !row.reset || !row.validate ||
          !row.validate(row.object, row.id, row.kind))
        return false;
    }
    return true;
  }

private:
  std::span<const DispatchRow> rows_;
};

} // namespace gfsim

#endif // GFSIM_DISPATCH_H
