//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.h
//
// Identification: src/include/buffer/lru_k_replacer.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <limits>
#include <list>
#include <mutex>  // NOLINT
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/macros.h"

namespace bustub {

enum class AccessType : char { Unknown = 0, Lookup, Scan, Index };

// LRUKNode 是 LRUKReplacer 的 metainfo 条目类型,
// 只保存 page id `fid_` 和对应 page 的访问历史时间戳 `history_`, 不管理 page 实际数据.
class LRUKNode {
 private:
  /** History of last seen K timestamps of this page. Least recent timestamp stored in front. */
  // Remove maybe_unused if you start using them. Feel free to change the member variables as you want.

  [[maybe_unused]] size_t k_;
  [[maybe_unused]] std::vector<size_t> history_;  // history of access timestamps.
  [[maybe_unused]] size_t beg_idx_{};
  [[maybe_unused]] size_t end_idx_{};
  [[maybe_unused]] frame_id_t fid_;
  [[maybe_unused]] bool is_evictable_{false};

 public:
  LRUKNode() = default;
  ~LRUKNode() = default;

  DISALLOW_COPY_AND_MOVE(LRUKNode);

  LRUKNode(frame_id_t id, size_t k);

  void SetNewTimestamp(size_t time);

  auto GetPreviousKthHistory() -> size_t;

  auto GetOldestHistory() -> size_t;

  void SetEvictable(bool evictable);

  auto CheckEvictable() -> bool;

  void ClearHistory();
};

/**
 * LRUKReplacer implements the LRU-k replacement policy.
 *
 * The LRU-k algorithm evicts a frame whose backward k-distance is maximum
 * of all frames. Backward k-distance is computed as the difference in time between
 * current timestamp and the timestamp of kth previous access.
 *
 * NOTICE: evicts only one frame.
 *
 * A frame with less than k historical references is given
 * +inf as its backward k-distance. When multiple frames have +inf backward k-distance,
 * classical LRU algorithm is used to choose victim.
 *
 * NOTICE: If history length is less than `k`, then the most previous timestamp is `+inf`.
 * LRUKReplacer 是个 metaclass, 其中维护了所有的 page id,
 * 仅记录对 page 的访问历史, 用作实现 page 淘汰逻辑。
 */
class LRUKReplacer {
 public:
  LRUKReplacer(size_t num_frames, size_t k);

  DISALLOW_COPY_AND_MOVE(LRUKReplacer);

  /**
   * TODO(P1): Add implementation
   *
   * @brief Destroys the LRUReplacer.
   */
  ~LRUKReplacer() = default;

  auto Evict() -> std::optional<frame_id_t>;

  void RecordAccess(frame_id_t frame_id, AccessType access_type = AccessType::Unknown);

  void SetEvictable(frame_id_t frame_id, bool set_evictable);

  void Remove(frame_id_t frame_id);

  auto Size() -> size_t;

 private:
  // TODO(student): implement me! You can replace these member variables as you like.
  // Remove maybe_unused if you start using them.
  [[maybe_unused]] std::unordered_map<frame_id_t, LRUKNode> node_store_;
  [[maybe_unused]] size_t current_timestamp_{1};
  [[maybe_unused]] size_t curr_size_{};
  [[maybe_unused]] size_t replacer_size_{};
  [[maybe_unused]] size_t k_;
  [[maybe_unused]] std::mutex latch_;
};

}  // namespace bustub
