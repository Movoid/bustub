//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.h
//
// Identification: src/include/buffer/lru_k_replacer.h
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
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

  size_t k_;
  size_t next_idx_{};
  std::vector<size_t> history_;  // History of access timestamps.
  [[maybe_unused]] frame_id_t fid_;
  bool is_evictable_{false};

 public:
  ~LRUKNode() = default;

  LRUKNode(const LRUKNode &) = delete;
  auto operator=(const LRUKNode &) -> LRUKNode & = delete;
  LRUKNode(LRUKNode &&) noexcept = default;                      // 允许移动
  auto operator=(LRUKNode &&) noexcept -> LRUKNode & = default;  // 允许移动

  LRUKNode(frame_id_t id, size_t k);

  void Record(size_t time);

  auto GetPreviousKthHistory() -> size_t;

  auto GetOldestHistory() -> size_t;

  void SetEvictable(bool evictable);

  auto CheckEvictable() -> bool;

  void Reset();
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
 *
 * LRUKReplacer 是个 metaclass, 其中维护了所有的 page id,
 * 仅记录对 page 的访问历史, 用作实现 page 淘汰逻辑。
 *
 * 注意, Replacer 实际淘汰的是 page, 每个活跃的 page 占据一个 frame,
 * 而不活跃的 page, Replacer 会将其 evict, 并返回其 frame id,
 * 此 frame 将被 BufferPoolMannager 清空, 用于新 load 到 memory 的 page.
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
  // std::unordered_map<frame_id_t, LRUKNode> node_store_;
  std::vector<LRUKNode> node_store_;
  size_t current_timestamp_{1};
  const size_t max_size_;
  size_t replacer_size_{};
  size_t k_;
  std::mutex latch_;
};

}  // namespace bustub
