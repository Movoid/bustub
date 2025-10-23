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
#include <random>
#include "common/exception.h"

namespace bustub {

// ========================================
//
// LEGACY
//
// ========================================

LRUKReplacer::LRUKNode::LRUKNode(frame_id_t id, size_t k) : k_{k}, history_(k, 0), fid_{id} {
  BUSTUB_ASSERT(k > 0, "LRUKNode::LRUKNode: k must > 0.");
};

void LRUKReplacer::LRUKNode::Record(size_t time) {
  BUSTUB_ASSERT(time > 0, "LRUKNode::SetNewTimestamp: time must > 0.");
  history_[next_idx_] = time;
  next_idx_ = (next_idx_ + 1) % k_;
}

auto LRUKReplacer::LRUKNode::GetPreviousKthHistory() -> size_t { return history_[next_idx_]; }

auto LRUKReplacer::LRUKNode::GetOldestHistory() -> size_t {
  return history_[next_idx_] == 0 ? history_[0] : history_[next_idx_];
}

void LRUKReplacer::LRUKNode::SetEvictable(bool evictable) { is_evictable_ = evictable; }

auto LRUKReplacer::LRUKNode::CheckEvictable() -> bool { return is_evictable_; }

void LRUKReplacer::LRUKNode::Reset() {
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

// ========================================
//
// IMPROVED
//
// ========================================

// ========================================
//
// ShardingLRUKReplacer::LRUKNode
//
// ========================================
ShardingLRUKReplacer::LRUKNode::LRUKNode(frame_id_t frame_id, size_t k) : frame_id_{frame_id}, k_{k}, history_(k, 0) {};

auto ShardingLRUKReplacer::LRUKNode::RecordTime(std::uint64_t time) -> std::uint64_t {
  history_[visit_counter_ % history_.size()] = time;
  auto cur_visit{visit_counter_++};
  return cur_visit;
}

void ShardingLRUKReplacer::LRUKNode::ResetHistory() {
  history_[0] = 0;
  visit_counter_ = 0;
}

auto ShardingLRUKReplacer::LRUKNode::IsHotPage() -> bool { return visit_counter_ >= k_; }

auto ShardingLRUKReplacer::LRUKNode::GetPreviousKthHistory() -> std::uint64_t {
  BUSTUB_ASSERT(IsHotPage(), "Unexpected.");
  return history_[visit_counter_ % history_.size()];
}

auto ShardingLRUKReplacer::LRUKNode::GetEarliestHistory() -> std::uint64_t {
  BUSTUB_ASSERT(!IsHotPage(), "Unexpected.");
  return history_[0];
}

auto ShardingLRUKReplacer::LRUKNode::Pin() -> std::uint64_t { return pin_cnt_++; }

auto ShardingLRUKReplacer::LRUKNode::Unpin() -> std::uint64_t { return pin_cnt_--; }

auto ShardingLRUKReplacer::LRUKNode::GetPinCnt() -> std::uint64_t { return pin_cnt_; }

auto ShardingLRUKReplacer::LRUKNode::IsEvictable() -> bool { return pin_cnt_ == 0; }

// ========================================
//
// ShardingLRUKReplacer::EarlyTimeFirstCmp
//
// ========================================

auto ShardingLRUKReplacer::EarlyTimeFirstCmp::operator()(std::uint64_t a, std::uint64_t b) -> bool { return a > b; }

// ========================================
//
// ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard
//
// ========================================

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::TryEvictColdUnsafe(frame_id_t &evicted_frame_id)
    -> bool {
  if (cold_heap_.Size() == 0) {
    return false;
  }
  auto to_node{cold_heap_.Top().first};
  BUSTUB_ASSERT(to_node->pin_cnt_ == 0, "Unexpected.");
  cold_heap_.Pop();
  cold_evictable_cnt_approx_.fetch_sub(1, std::memory_order_relaxed);
  to_node->Pin();
  evicted_frame_id = to_node->frame_id_;
  return true;
}

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::TryEvictHotUnsafe(frame_id_t &evicted_frame_id)
    -> bool {
  if (hot_heap_.Size() == 0) {
    return false;
  }
  auto to_node{hot_heap_.Top().first};
  BUSTUB_ASSERT(to_node->pin_cnt_ == 0, "Unexpected.");
  hot_heap_.Pop();
  hot_evictable_cnt_approx_.fetch_sub(1, std::memory_order_relaxed);
  to_node->Pin();
  evicted_frame_id = to_node->frame_id_;
  return true;
}

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::PinUnsafe(ShardingLRUKReplacer::LRUKNode *to_node)
    -> std::uint64_t {
  bool old_is_hot{to_node->IsHotPage()};
  auto old_pin_cnt{to_node->Pin()};
  if (old_pin_cnt == 0) {
    auto to_heap{old_is_hot ? &hot_heap_ : &cold_heap_};
    auto to_cnt_approx{old_is_hot ? &hot_evictable_cnt_approx_ : &cold_evictable_cnt_approx_};
    // BUSTUB_ASSERT(to_heap->Contains(to_node), "Unexpected.");
    // 初始时, Heap 空, 允许此时 `PinUnsafe` 不操作 Heap.
    if (to_heap->Contains(to_node)) {
      to_heap->Erase(to_node);
      to_cnt_approx->fetch_sub(1, std::memory_order_relaxed);
    }
  }
  return old_pin_cnt;
}

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::UnpinUnsafe(ShardingLRUKReplacer::LRUKNode *to_node)
    -> std::uint64_t {
  auto old_pin_cnt{to_node->Unpin()};
  BUSTUB_ASSERT(old_pin_cnt > 0, "Unexpected.");
  BUSTUB_ASSERT(!cold_heap_.Contains(to_node), "Unexpected.");
  BUSTUB_ASSERT(!hot_heap_.Contains(to_node), "Unexpected.");
  if (old_pin_cnt == 1) {
    auto to_heap{to_node->IsHotPage() ? &hot_heap_ : &cold_heap_};
    auto to_cnt_approx{to_node->IsHotPage() ? &hot_evictable_cnt_approx_ : &cold_evictable_cnt_approx_};
    to_heap->Push(to_node, to_node->IsHotPage() ? to_node->GetPreviousKthHistory() : to_node->GetEarliestHistory());
    to_cnt_approx->fetch_add(1, std::memory_order_relaxed);
  }
  return old_pin_cnt;
}

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::ColdEvictableSizeApprox() -> std::size_t {
  return cold_evictable_cnt_approx_.load(std::memory_order_relaxed);
}

auto ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerShard::HotEvictableSizeApprox() -> std::size_t {
  return hot_evictable_cnt_approx_.load(std::memory_order_relaxed);
}

// ========================================
//
// ShardingLRUKReplacer::LRUKReplacerTable
//
// ========================================

ShardingLRUKReplacer::LRUKReplacerTable::LRUKReplacerTable(std::size_t shard_cnt) : shards_(shard_cnt) {}

auto ShardingLRUKReplacer::LRUKReplacerTable::VisitShard(frame_id_t frame_id) -> LRUKReplacerShard * {
  return &shards_[FastHash{}(frame_id, 1) % shards_.size()];
}

// ========================================
//
// ShardingLRUKReplacer
//
// ========================================

ShardingLRUKReplacer::ShardingLRUKReplacer(size_t num_frames, size_t k)
    : max_size_{num_frames}, k_(k), table_(std::max(1, static_cast<int>(num_frames / MAX_FRAME_PER_SHARD))) {
  BUSTUB_ASSERT(num_frames < std::numeric_limits<frame_id_t>::max(), "Invalid args.");
  for (size_t i = 0; i < num_frames; i++) {
    nodes_.emplace_back(static_cast<frame_id_t>(i), k);
  }
}

auto ShardingLRUKReplacer::SelectEvictAndPin() -> std::optional<frame_id_t> {
  static thread_local std::mt19937 rd_gen{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> dis{0, table_.shards_.size() - 1};
  constexpr std::uint64_t retry_limit{3};
  std::uint64_t retry_times{};
  while (true) {
    // Power of 2 choices.
    auto cand_shard_id_a{dis(rd_gen)};
    auto cand_shard_id_b{dis(rd_gen)};

    auto shard_id{table_.shards_[cand_shard_id_a].ColdEvictableSizeApprox() >
                          table_.shards_[cand_shard_id_b].ColdEvictableSizeApprox()
                      ? cand_shard_id_a
                      : cand_shard_id_b};

    auto to_shard{&table_.shards_[shard_id]};

    /**
     * It's about to OOM.
     */
    if (retry_times >= retry_limit && to_shard->ColdEvictableSizeApprox() == 0) {
      LOG_WARN("ShardingLRUKReplacer retried too many times!");
      for (std::size_t iter = 0; iter < table_.shards_.size(); iter++) {
        auto to_iter_shard{&table_.shards_[(shard_id + iter) % table_.shards_.size()]};
        if (to_iter_shard->ColdEvictableSizeApprox() + to_iter_shard->HotEvictableSizeApprox() > 0) {
          to_shard = to_iter_shard;
          break;
        }
      }
      if (to_shard->ColdEvictableSizeApprox() + to_shard->HotEvictableSizeApprox() == 0) {
        return std::nullopt;
      }
    }

    frame_id_t evicted_frame_id{INVALID_FRAME_ID};
    {
      std::lock_guard lk{to_shard->m_};
      if (to_shard->TryEvictColdUnsafe(evicted_frame_id)) {
        return evicted_frame_id;
      }
    }

    /**
     * Failed evicting in the current shard.
     * Try work stealing.
     */
    auto to_sib_shard{&table_.shards_[(shard_id + 1) % table_.shards_.size()]};
    {
      std::lock_guard lk{to_sib_shard->m_};
      if (to_sib_shard->TryEvictColdUnsafe(evicted_frame_id)) {
        return evicted_frame_id;
      }
    }

    /**
     * Still failed.
     * Back and try evict hot.
     */
    {
      std::lock_guard lk{to_shard->m_};
      if (to_shard->TryEvictColdUnsafe(evicted_frame_id)) {
        return evicted_frame_id;
      }
      if (to_shard->TryEvictHotUnsafe(evicted_frame_id)) {
        return evicted_frame_id;
      }
    }

    /**
     * Failed at last.
     * Restart `SelectEvict`.
     */
    retry_times++;
  }
}

/**
 * 由于 `SelectEvict` 后,
 * 仍允许其他 Worker 通过 `Pin`/`Unpin` 来操作被移出 Heap 的 LRUKNode,
 * 这使得 `TryCommitEvict` 可能失败.
 */
auto ShardingLRUKReplacer::TryCommitEvict(frame_id_t frame_id) -> bool {
  auto to_shard{table_.VisitShard(frame_id)};
  auto to_node{&nodes_[frame_id]};
  BUSTUB_ASSERT(!to_shard->cold_heap_.Contains(to_node), "Unexpected.");
  BUSTUB_ASSERT(!to_shard->hot_heap_.Contains(to_node), "Unexpected.");
  /**
   * lock 是必要的.
   * LRUKNode 的 `pin_cnt_` 变化应该和 ReplacerShard 的 Heap 变化是原子的.
   */
  {
    std::lock_guard lk{to_shard->m_};
    if (to_node->GetPinCnt() > 1) {
      to_node->Unpin();
      return false;
    }

    to_node->ResetHistory();
    return true;
  }
}

void ShardingLRUKReplacer::Pin(frame_id_t frame_id) {
  auto to_shard{table_.VisitShard(frame_id)};
  auto to_node{&nodes_[frame_id]};

  {
    std::lock_guard lk{to_shard->m_};
    to_shard->PinUnsafe(to_node);
  }
}

void ShardingLRUKReplacer::Unpin(frame_id_t frame_id) {
  auto to_shard{table_.VisitShard(frame_id)};
  auto to_node{&nodes_[frame_id]};

  {
    std::lock_guard lk{to_shard->m_};
    to_shard->UnpinUnsafe(to_node);
  }
}

/**
 * 可以将 `LRUKNode` 实现成无锁修改,
 * 但需要维护一个多生产者 RingBuffer.
 * Keep it simple.
 */
void ShardingLRUKReplacer::RecordAccess(frame_id_t frame_id, AccessType access_type) {
  auto to_shard{table_.VisitShard(frame_id)};
  auto to_node{&nodes_[frame_id]};
  {
    std::lock_guard lk{to_shard->m_};
    to_node->RecordTime(to_shard->time_counter_++);
    if (to_node->IsEvictable()) {
      auto to_heap{to_node->IsHotPage() ? &to_shard->hot_heap_ : &to_shard->cold_heap_};
      BUSTUB_ASSERT(to_heap->Contains(to_node), "Unexpected.");
      to_heap->Modify(to_node, to_node->IsHotPage() ? to_node->GetPreviousKthHistory() : to_node->GetEarliestHistory());
    }
  }
}

}  // namespace bustub
