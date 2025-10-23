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

#include <deque>
#include <limits>
#include <list>
#include <mutex>  // NOLINT
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"

namespace bustub {

enum class AccessType : char { Unknown = 0, Lookup, Scan, Index };

// ========================================
//
// LEGACY
//
// ========================================

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
  // Remove maybe_unused if you start using them.
  // std::unordered_map<frame_id_t, LRUKNode> node_store_;
  std::vector<LRUKNode> node_store_;
  size_t current_timestamp_{1};
  const size_t max_size_;
  size_t replacer_size_{};
  size_t k_;
  std::mutex latch_;
};

// ========================================
//
// IMPROVED
//
// ========================================

template <typename KeyType, typename CmpValType, typename CmpType = std::less<CmpValType>,
          typename AllocatorType = std::allocator<std::pair<KeyType, CmpValType>>>
class IndexedHeap {
 private:
  using BaseContainer = std::vector<std::pair<KeyType, CmpValType>, AllocatorType>;
  using IndexContainer = std::unordered_map<KeyType, std::size_t>;
  BaseContainer heap_;
  IndexContainer index_;
  CmpType cmp_;

 protected:
  static std::size_t Parent(std::size_t i) {
    assert(i > 0 && "Invalid args.");
    return (i - 1) / 2;
  }

  static auto LeftChild(std::size_t i) -> std::size_t { return (i << 1) + 1; }
  static auto RightChild(std::size_t i) -> std::size_t { return (i << 1) + 2; }

  void SwapKv(std::size_t a, std::size_t b) {
    using std::swap;
    swap(heap_[a], heap_[b]);
    index_[heap_[a].first] = a;  // Sync index.
    index_[heap_[b].first] = b;
  }

  void AdjustUp(std::size_t cur) {
    while (cur > 0) {
      auto parent_idx{Parent(cur)};
      if (cmp_(heap_[parent_idx].second, heap_[cur].second)) {
        SwapKv(parent_idx, cur);
        cur = parent_idx;
        continue;
      }
      break;
    }
  }

  void AdjustDown(std::size_t cur) {
    std::size_t n{heap_.size()};
    while (true) {
      std::size_t to{cur};
      std::size_t l{LeftChild(cur)};
      std::size_t r{RightChild(cur)};
      if (l < n && cmp_(heap_[to].second, heap_[l].second)) {
        to = l;
      }
      if (r < n && cmp_(heap_[to].second, heap_[r].second)) {
        to = r;
      }
      if (to == cur) {
        break;
      }
      SwapKv(to, cur);
      cur = to;
    }
  }

 public:
  explicit IndexedHeap(const CmpType &cmp = CmpType{}) : cmp_{cmp} {}
  auto Base() -> BaseContainer * { return &heap_; }

  auto Contains(const KeyType &key) const -> bool { return index_.find(key) != index_.end(); }

  auto Push(const KeyType &key, const CmpValType &val) -> bool {
    if (Contains(key)) {
      return false;
    }
    heap_.emplace_back(key, val);
    std::size_t idx{heap_.size() - 1};
    index_[key] = idx;
    AdjustUp(idx);
    assert(Contains(key) && "Unexpected.");
    return true;
  }

  auto Modify(const KeyType &key, const CmpValType &val) -> bool {
    if (!Contains(key)) {
      return false;
    }
    std::size_t cur_idx{index_[key]};
    auto old_val{heap_[cur_idx].second};
    heap_[cur_idx].second = val;
    if (cmp_(old_val, val)) {
      AdjustUp(cur_idx);
    } else if (cmp_(val, old_val)) {
      AdjustDown(cur_idx);
    }
    return true;
  }

  auto Erase(const KeyType &key) -> bool {
    if (!Contains(key)) {
      return false;
    }
    std::size_t cur_idx{index_[key]};
    std::size_t last_idx{heap_.size() - 1};
    if (cur_idx != last_idx) {
      SwapKv(cur_idx, last_idx);
    }
    index_.erase(heap_[last_idx].first);
    heap_.pop_back();
    if (cur_idx < heap_.size()) {
      AdjustDown(cur_idx);
      AdjustUp(cur_idx);  // 只有一个 `Adjust*` 有效.
    }
    assert(!Contains(key) && "Unexpected.");
    return true;
  }

  auto Top() const -> const std::pair<KeyType, CmpValType> & {
    assert(!heap_.empty() && "Unexpected.");
    return heap_[0];
  }

  void Pop() {
    assert(!heap_.empty() && "Unexpected.");
    KeyType key{heap_[0].first};
    Erase(key);
  }

  auto Empty() -> bool { return heap_.empty(); }

  auto Size() -> std::size_t { return heap_.size(); }
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
 */
class ShardingLRUKReplacer {
 public:
  ShardingLRUKReplacer(size_t num_frames, size_t k);

  DISALLOW_COPY_AND_MOVE(ShardingLRUKReplacer);

  /**
   * TODO(P1): Add implementation
   *
   * @brief Destroys the LRUReplacer.
   */
  ~ShardingLRUKReplacer() = default;

  auto SelectEvictAndPin() -> std::optional<frame_id_t>;
  [[nodiscard]] auto TryCommitEvict(frame_id_t frame_id) -> bool;

  void Pin(frame_id_t frame_id);
  void Unpin(frame_id_t frame_id);

  void RecordAccess(frame_id_t frame_id, AccessType access_type = AccessType::Unknown);

 private:
  // LRUKNode 是 LRUKReplacer 的 metainfo 条目类型,
  // 只保存 page id `fid_` 和对应 page 的访问历史时间戳 `history_`, 不管理 page 实际数据.
  struct LRUKNode {
    frame_id_t const frame_id_{INVALID_FRAME_ID};
    std::size_t const k_{};

    std::vector<std::uint64_t> history_;
    std::uint64_t visit_counter_{};

    std::uint64_t pin_cnt_{};

    ~LRUKNode() = default;

    LRUKNode(const LRUKNode &) = delete;
    auto operator=(const LRUKNode &) -> LRUKNode & = delete;
    LRUKNode(LRUKNode &&) noexcept = default;  // 允许 `std::vector`
    auto operator=(LRUKNode &&) noexcept -> LRUKNode & = delete;

    LRUKNode(frame_id_t id, std::size_t k);

    auto RecordTime(std::uint64_t time) -> std::uint64_t;

    void ResetHistory();

    auto IsHotPage() -> bool;

    auto GetPreviousKthHistory() -> std::uint64_t;

    auto GetEarliestHistory() -> std::uint64_t;

    auto Pin() -> std::uint64_t;

    auto Unpin() -> std::uint64_t;

    auto GetPinCnt() -> std::uint64_t;

    auto IsEvictable() -> bool;
  };

  constexpr static std::size_t MAX_FRAME_PER_SHARD{16};

  std::size_t const max_size_{};
  std::size_t const k_{};

  /**
   * 所有的 LRUKNode 存放于此.
   */
  std::vector<LRUKNode> nodes_;

  struct EarlyTimeFirstCmp {
    auto operator()(std::uint64_t a, std::uint64_t b) -> bool;
  };

  /**
   * 通过 Work Stealing 缓解非最优 Evict 问题.
   */
  struct LRUKReplacerTable {
    /**
     * 可被 Evict 的 frames of unpinned pages.
     * 对于被 Pin `0 -> 1` 的 frame, 直接移出 `heap_`.
     *
     * > 最初 Heap 是空的.
     */
    struct alignas(simple_cu::utils::CACHELINE_SIZE) LRUKReplacerShard {
      std::mutex m_;
      std::condition_variable cv_;
      std::uint64_t time_counter_{1};
      IndexedHeap<LRUKNode *, std::uint64_t, EarlyTimeFirstCmp> hot_heap_;
      IndexedHeap<LRUKNode *, std::uint64_t, EarlyTimeFirstCmp> cold_heap_;
      std::atomic<std::uint64_t> hot_evictable_cnt_approx_{};
      std::atomic<std::uint64_t> cold_evictable_cnt_approx_{};

      // Auto pin.
      auto TryEvictColdUnsafe(frame_id_t &evicted_frame_id) -> bool;
      auto TryEvictHotUnsafe(frame_id_t &evicted_frame_id) -> bool;

      auto PinUnsafe(LRUKNode *to_node) -> std::uint64_t;
      auto UnpinUnsafe(LRUKNode *to_node) -> std::uint64_t;

      auto ColdEvictableSizeApprox() -> std::size_t;
      auto HotEvictableSizeApprox() -> std::size_t;
    };

    std::vector<LRUKReplacerShard> shards_;

    explicit LRUKReplacerTable(std::size_t shard_cnt);
    auto VisitShard(frame_id_t frame_id) -> LRUKReplacerShard *;
  };

  LRUKReplacerTable table_;
};

}  // namespace bustub
