#ifndef GFSIM_RESOURCE_H
#define GFSIM_RESOURCE_H

#include "gfsim/core.h"
#include "gfsim/object.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gfsim {

class Resource : public SimObject {
public:
  Resource(std::string name, ObjectId id, SimObject *parent,
           uint32_t totalCapacity)
      : SimObject(ObjectKind::Resource, std::move(name), id, parent),
        totalCapacity_(totalCapacity) {}

  uint32_t totalCapacity() const { return totalCapacity_; }
  uint32_t availableCapacity() const {
    return totalCapacity_ - activeReservations_;
  }
  uint32_t activeReservations() const { return activeReservations_; }
  bool canReserve(uint32_t amount = 1) const {
    return availableCapacity() >= amount + proposedReservations_;
  }

  struct ReservationProposal {
    ObjectId ownerId;
    uint32_t amount;
    Epoch issueTime;
    uint64_t transactionId;
  };

  bool proposeReserve(ObjectId owner, uint32_t amount, Epoch issueTime,
                      uint64_t txnId) {
    if (!canReserve(amount))
      return false;
    proposals_.push_back({owner, amount, issueTime, txnId});
    proposedReservations_ += amount;
    return true;
  }

  bool proposeRelease(ObjectId owner, uint32_t amount) {
    releaseProposals_.push_back({owner, amount});
    return true;
  }

  void doArbitrate(Epoch) override {
    acceptedProposals_.clear();
    rejectedProposals_.clear();
    uint32_t remaining = availableCapacity();
    for (auto &p : proposals_) {
      if (p.amount <= remaining) {
        acceptedProposals_.push_back(p);
        remaining -= p.amount;
      } else {
        rejectedProposals_.push_back(p);
      }
    }
  }

  void doXfer(Epoch) override {
    for (auto &p : acceptedProposals_) {
      activeReservations_ += p.amount;
      totalReservations_ += p.amount;
    }
    for (auto &r : releaseProposals_) {
      if (r.amount <= activeReservations_)
        activeReservations_ -= r.amount;
      totalReleases_ += r.amount;
    }
    proposals_.clear();
    acceptedProposals_.clear();
    rejectedProposals_.clear();
    releaseProposals_.clear();
    proposedReservations_ = 0;
    if (activeReservations_ > highWatermark_)
      highWatermark_ = activeReservations_;
  }

  bool hasPendingCommit() const override {
    return !acceptedProposals_.empty() || !releaseProposals_.empty();
  }

  uint32_t highWatermark() const { return highWatermark_; }
  uint64_t totalReservations() const { return totalReservations_; }
  uint64_t totalReleases() const { return totalReleases_; }

  void reset() override {
    activeReservations_ = 0;
    proposedReservations_ = 0;
    proposals_.clear();
    acceptedProposals_.clear();
    rejectedProposals_.clear();
    releaseProposals_.clear();
    highWatermark_ = 0;
    totalReservations_ = 0;
    totalReleases_ = 0;
  }

private:
  uint32_t totalCapacity_;
  uint32_t activeReservations_ = 0;
  uint32_t proposedReservations_ = 0;
  uint32_t highWatermark_ = 0;
  uint64_t totalReservations_ = 0;
  uint64_t totalReleases_ = 0;
  std::vector<ReservationProposal> proposals_;
  std::vector<ReservationProposal> acceptedProposals_;
  std::vector<ReservationProposal> rejectedProposals_;
  struct ReleaseProposal {
    ObjectId ownerId;
    uint32_t amount;
  };
  std::vector<ReleaseProposal> releaseProposals_;
};

} // namespace gfsim
#endif
