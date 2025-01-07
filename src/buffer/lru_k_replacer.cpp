//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include "common/exception.h"

namespace bustub {

LRUKNode::LRUKNode(frame_id_t id, size_t k) : k_{k}, history_(k, 0), fid_{id} {
  BUSTUB_ASSERT(k > 0, "LRUKNode::LRUKNode: k must > 0.");
};

void LRUKNode::Record(size_t time) {
  BUSTUB_ASSERT(time > 0, "LRUKNode::SetNewTimestamp: time must > 0.");
  history_[next_idx_] = time;
  next_idx_ = (next_idx_ + 1) % k_;
}

auto LRUKNode::GetPreviousKthHistory() -> size_t { return history_[next_idx_]; }

auto LRUKNode::GetOldestHistory() -> size_t { return history_[next_idx_] == 0 ? history_[0] : history_[next_idx_]; }

void LRUKNode::SetEvictable(bool evictable) { is_evictable_ = evictable; }

auto LRUKNode::CheckEvictable() -> bool { return is_evictable_; }

void LRUKNode::Reset() {
  next_idx_ = 0;
  std::fill(history_.begin(), history_.end(), 0);
  is_evictable_ = false;
}

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new LRUKReplacer.
 * @param num_frames the maximum number of frames the LRUReplacer will be required to store
 */
LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : max_size_{num_frames}, k_(k) {
  // node_store_.resize(num_frames);
  node_store_.reserve(num_frames);
  for (size_t i = 0; i < num_frames; i++) {
    node_store_.emplace_back(i, k_);
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Find the frame with largest backward k-distance and evict that frame. Only frames
 * that are marked as 'evictable' are candidates for eviction.
 *
 * A frame with less than k historical references is given +inf as its backward k-distance.
 * If multiple frames have inf backward k-distance, then evict frame whose oldest timestamp
 * is furthest in the past.
 *
 * Successful eviction of a frame should decrement the size of replacer and remove the frame's
 * access history.
 *
 * @return the frame ID if a frame is successfully evicted, or `std::nullopt` if no frames can be evicted.
 */
auto LRUKReplacer::Evict() -> std::optional<frame_id_t> {
  bool can_evict{};
  frame_id_t to_evict{INVALID_FRAME_ID};
  size_t min_kth_history{std::numeric_limits<size_t>::max()};
  size_t min_oldest_history{std::numeric_limits<size_t>::max()};
  {
    std::lock_guard<std::mutex> guard{latch_};
    for (size_t i = 0; i < max_size_; i++) {
      LRUKNode &node{node_store_[i]};
      frame_id_t fid{static_cast<frame_id_t>(i)};
      if (!node.CheckEvictable()) {
        continue;
      }
      can_evict = true;
      size_t kth_history{node.GetPreviousKthHistory()};
      size_t oldest_history{node.GetOldestHistory()};
      if (kth_history == 0 && oldest_history < min_oldest_history) {  // new cold page
        min_kth_history = 0;
        min_oldest_history = oldest_history;
        to_evict = fid;
      } else if (kth_history != 0 && kth_history < min_kth_history) {  // old cold page
        min_kth_history = kth_history;
        to_evict = fid;
      }
    }

    if (!can_evict) {
      return std::nullopt;
    }
    node_store_[to_evict].Reset();
    replacer_size_--;
  }
  return std::make_optional(to_evict);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record the event that the given frame id is accessed at current timestamp.
 * Create a new entry for access history if frame id has not been seen before.
 *
 * If frame id is invalid (ie. larger than replacer_size_), throw an exception. You can
 * also use BUSTUB_ASSERT to abort the process if frame id is invalid.
 *
 * @param frame_id id of frame that received a new access.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  {
    std::lock_guard<std::mutex> guard{latch_};
    node_store_[frame_id].Record(current_timestamp_++);
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  {
    std::lock_guard<std::mutex> guard{latch_};
    if (node_store_[frame_id].CheckEvictable() == set_evictable) {
      return;
    }
    if (set_evictable) {
      replacer_size_++;
    } else {
      replacer_size_--;
    }
    node_store_[frame_id].SetEvictable(set_evictable);
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer, along with its access history.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * with largest backward k-distance. This function removes specified frame id,
 * no matter what its backward k-distance is.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void LRUKReplacer::Remove(frame_id_t frame_id) {
  {
    std::lock_guard<std::mutex> guard{latch_};
    if (node_store_[frame_id].CheckEvictable()) {
      replacer_size_--;  // Important!
    }
    node_store_[frame_id].Reset();
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto LRUKReplacer::Size() -> size_t {
  {
    std::lock_guard<std::mutex> guard{latch_};
    return replacer_size_;
  }
}

}  // namespace bustub
