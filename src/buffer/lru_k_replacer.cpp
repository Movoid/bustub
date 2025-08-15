//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include "common/exception.h"

namespace bustub {

LRUKNode::LRUKNode(frame_id_t id, size_t k) : k_{k}, history_(k, 0), fid_{id} {};

void LRUKNode::SetNewTimestamp(size_t time) {
  BUSTUB_ASSERT(time > 0, "LRUKNode::SetNewTimestamp: time should > 0.");
  history_[end_idx_] = time;
  end_idx_ = (end_idx_ + 1) % k_;
  if (end_idx_ == beg_idx_) {
    beg_idx_ = (beg_idx_ + 1) % k_;
  }
}

auto LRUKNode::GetPreviousKthHistory() -> size_t { return history_[end_idx_]; }

auto LRUKNode::GetOldestHistory() -> size_t { return history_[beg_idx_]; }

void LRUKNode::SetEvictable(bool evictable) { is_evictable_ = evictable; }

auto LRUKNode::CheckEvictable() -> bool { return is_evictable_; }

void LRUKNode::ClearHistory() { history_.clear(); }

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new LRUKReplacer.
 * @param num_frames the maximum number of frames the LRUReplacer will be required to store
 */
LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : curr_size_{num_frames}, k_(k) {}

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
  frame_id_t to_evict{};
  size_t min_kth_history{std::numeric_limits<size_t>::max()};
  size_t min_oldest_history{std::numeric_limits<size_t>::max()};
  {
    std::lock_guard<std::mutex> l{latch_};
    for (auto &node_iter : node_store_) {
      auto fid{node_iter.first};
      auto &node{node_iter.second};
      if (node.CheckEvictable()) {
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
    }

    if (!can_evict) {
      return std::nullopt;
    }
    node_store_.erase(to_evict);
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
    std::lock_guard<std::mutex> l{latch_};
    // std::cout << "frame_id " << frame_id << ", replacer_size " << replacer_size_ << std::endl;
    node_store_.try_emplace(frame_id, frame_id, k_);
    node_store_[frame_id].SetNewTimestamp(current_timestamp_++);
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
    std::lock_guard<std::mutex> l{latch_};
    node_store_.try_emplace(frame_id, frame_id, k_);
    if (node_store_[frame_id].CheckEvictable() != set_evictable) {
      if (set_evictable) {
        replacer_size_++;
        // std::cout << "SetEvictable true, size now " << replacer_size_ << std::endl;
      } else {
        replacer_size_--;
        // std::cout << "SetEvictable false, size now " << replacer_size_ << std::endl;
      }
      node_store_[frame_id].SetEvictable(set_evictable);
    }
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
    std::lock_guard<std::mutex> l{latch_};
    if (node_store_.find(frame_id) != node_store_.end()) {  // NOLINT
      node_store_[frame_id].ClearHistory();
    }
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
    std::lock_guard<std::mutex> l{latch_};
    return replacer_size_;
  }
}

}  // namespace bustub
