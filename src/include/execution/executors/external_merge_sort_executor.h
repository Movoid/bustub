//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Spring 2025.
 */
class SortPage {
 public:
  /**
   * TODO(P3): Define and implement the methods for reading data from and writing data to the sort
   * page. Feel free to add other helper methods.
   */

  SortPage() = delete;
  SortPage(const SortPage &) = delete;
  ~SortPage() = delete;

  void Init(const Schema &schema) {
    BUSTUB_ASSERT(schema.IsInlined(), "Unsupported.");
    /**
     * Tuple 的 `SerializeTo` 会先写 int32_t 的 `data_.size()`.
     */
    unit_size_ = schema.GetInlinedStorageSize() + sizeof(int32_t);
    size_ = 0;
    capacity_ = DATA_SIZE / unit_size_;
  }

  auto GetUnitSize() const -> uint32_t { return unit_size_; }
  auto GetSize() const -> uint32_t { return size_; }
  auto GetCapacity() const -> uint32_t { return capacity_; }

  auto IsFull() const -> bool { return size_ == capacity_; }

  auto GetSlot(uint32_t k) const -> const char * {
    BUSTUB_ASSERT(k < capacity_, "Unexpected.");
    return &data_[k * unit_size_];
  }

  void SerializeAppendData(const Tuple &tuple) {
    BUSTUB_ASSERT(!IsFull(), "Unexpected.");
    auto to_slot{&data_[size_ * unit_size_]};
    tuple.SerializeTo(to_slot);
    size_++;
  }

 private:
  /**
   * TODO(P3): Define the private members. You may want to have some necessary metadata for
   * the sort page before the start of the actual data.
   */

  constexpr static int32_t SORTPAGE_HEADER_SIZE{sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t)};
  constexpr static int32_t DATA_SIZE{BUSTUB_PAGE_SIZE - SORTPAGE_HEADER_SIZE};

  uint32_t unit_size_{};
  uint32_t size_{};
  uint32_t capacity_{};
  char data_[DATA_SIZE];
};

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 *
 * x pages 的 tuples 排序后仍是 x pages.
 * 每个 Run 包含至少 1 个 Page ID, Run 内的所有 Tuples 都已经有序.
 * k-way 归并将 k 个 Run 合并为一个 Run, 并返回给递归上层.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), to_bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  /**
   * ForwardIterator.
   */
  class Iterator {
    friend class MergeSortRun;

   public:
    auto operator++() -> Iterator & {
      BUSTUB_ASSERT(next_page_pos_ < static_cast<uint32_t>(to_run_->pages_.size()), "Unexpected.");
      auto to_sortpage{page_g_.As<SortPage>()};
      next_in_page_slot_pos_++;

      if (next_in_page_slot_pos_ == to_sortpage->GetSize()) {
        next_in_page_slot_pos_ = 0;
        next_page_pos_++;
      }

      if (next_page_pos_ == static_cast<uint32_t>(to_run_->pages_.size())) {
        next_in_page_slot_pos_ = UINT32_MAX;
        next_page_pos_ = UINT32_MAX;
        page_g_.Drop();
        return *this;
      }

      page_g_ = to_run_->to_bpm_->ReadPage(to_run_->pages_[next_page_pos_]);
      to_sortpage = page_g_.As<SortPage>();
      BUSTUB_ASSERT(next_in_page_slot_pos_ < to_sortpage->GetSize(), "Unexpected.");
      return *this;
    }

    auto operator*() -> Tuple {
      auto to_sortpage{page_g_.As<SortPage>()};
      Tuple ret{};
      ret.DeserializeFrom(to_sortpage->GetSlot(next_in_page_slot_pos_));
      return ret;
    }

    auto operator==(const Iterator &other) const -> bool {
      return to_run_ == other.to_run_ && next_page_pos_ == other.next_page_pos_ &&
             next_in_page_slot_pos_ == other.next_in_page_slot_pos_;
    }

    auto operator!=(const Iterator &other) const -> bool { return !(*this == other); }

   private:
    Iterator(const MergeSortRun *run, uint32_t next_page_pos, uint32_t next_in_page_slot_pos)
        : to_run_{run}, next_page_pos_{next_page_pos}, next_in_page_slot_pos_{next_in_page_slot_pos} {
      if (next_page_pos < static_cast<uint32_t>(to_run_->pages_.size())) {
        page_g_ = to_run_->to_bpm_->ReadPage(to_run_->pages_[next_page_pos_]);
      }
    }

    const MergeSortRun *to_run_{};
    ReadPageGuard page_g_;
    uint32_t next_page_pos_{UINT32_MAX};
    uint32_t next_in_page_slot_pos_{UINT32_MAX};
  };

  auto Begin() -> Iterator {
    if (pages_.empty()) {
      return Iterator{this, UINT32_MAX, UINT32_MAX};
    }
    return Iterator{this, 0, 0};
  }

  auto End() -> Iterator { return Iterator{this, UINT32_MAX, UINT32_MAX}; }

 private:
  std::vector<page_id_t> pages_;

  BufferPoolManager *to_bpm_;
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Spring 2025, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  ~ExternalMergeSortExecutor() override;

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *to_plan_;

  std::unique_ptr<AbstractExecutor> to_child_executor_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;

  std::vector<page_id_t> sorted_pages_;
  MergeSortRun sorted_run_;
  std::optional<MergeSortRun::Iterator> iter_opt_;
  bool is_built_{};
};

}  // namespace bustub
