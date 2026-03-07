//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <vector>
#include "common/macros.h"
#include "execution/plans/sort_plan.h"

namespace bustub {

/**
 * 对于 `ORDER BY ... ASC`, 有 Index 则直接利用 BPlusTree.
 * 对于 `ORDER BY ... DESC`, 需要 ExternalMergeSortExecutor.
 *
 * k-way merge-sort.
 */
template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx},
      to_plan_{plan},
      to_child_executor_{std::move(child_executor)},
      cmp_{plan->GetOrderBy()} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the external merge sort */
template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
  to_child_executor_->Init();

  if (is_built_) {
    iter_opt_ = sorted_run_.Begin();
    return;
  }

  Tuple cur_tuple{};
  RID cur_rid{};
  auto status{to_child_executor_->Next(&cur_tuple, &cur_rid)};

  if (!status) {
    iter_opt_ = sorted_run_.Begin();
    is_built_ = true;
    return;
  }

  auto to_bpm{exec_ctx_->GetBufferPoolManager()};
  const auto &schema{to_child_executor_->GetOutputSchema()};

  std::vector<page_id_t> need_sort_pages{};
  {
    auto page_g_w{to_bpm->WritePage(to_bpm->NewPage())};
    auto to_sortpage{page_g_w.AsMut<SortPage>()};
    to_sortpage->Init(schema);
    while (status) {
      if (to_sortpage->IsFull()) {
        need_sort_pages.emplace_back(page_g_w.GetPageId());
        page_g_w = to_bpm->WritePage(to_bpm->NewPage());
        to_sortpage = page_g_w.AsMut<SortPage>();
        to_sortpage->Init(schema);
      }
      to_sortpage->SerializeAppendData(cur_tuple);
      status = to_child_executor_->Next(&cur_tuple, &cur_rid);
    }
    need_sort_pages.emplace_back(page_g_w.GetPageId());
  }

  struct ArgPack {
    int32_t l_idx_{};
    int32_t r_idx_{};
    bool backward_{};
    ArgPack(int32_t l_idx, int32_t r_idx, bool backward) : l_idx_{l_idx}, r_idx_{r_idx}, backward_{backward} {}
  };

  std::vector<ArgPack> callstack{};
  callstack.emplace_back(0, need_sort_pages.size(), false);
  while (!callstack.empty()) {
    auto &[l_idx_, r_idx_, backward_]{callstack.back()};
    if (!backward_) {
      int32_t mid_idx{l_idx_ + ((r_idx_ - l_idx_) / 2)};
      backward_ = true;
      if (r_idx_ - l_idx_ > 1) {
        int32_t l_idx{l_idx_};
        int32_t r_idx{r_idx_};
        callstack.emplace_back(l_idx, mid_idx, false);
        callstack.emplace_back(mid_idx, r_idx, false);
      }
      continue;
    }
    if (r_idx_ - l_idx_ == 1) {
      /**
       * Sort the single page.
       */
      BUSTUB_ASSERT(l_idx_ < static_cast<int32_t>(need_sort_pages.size()), "Unexpected.");
      std::vector<SortEntry> entries{};
      {
        auto page_g{to_bpm->WritePage(need_sort_pages[l_idx_])};
        auto to_sortpage{page_g.template AsMut<SortPage>()};
        entries.reserve(to_sortpage->GetSize());
        BUSTUB_ASSERT(to_sortpage->GetUnitSize() == schema.GetInlinedStorageSize() + sizeof(int32_t), "Unexpected.");
        /**
         * 注意 SortPage 中的存储格式.
         * Tuple 的反序列化需要提供指向数据的指针.
         */
        for (uint32_t i = 0; i < to_sortpage->GetSize(); i++) {
          Tuple cur_tuple{};
          cur_tuple.DeserializeFrom(to_sortpage->GetSlot(i));
          auto cur_sort_key{GenerateSortKey(cur_tuple, to_plan_->GetOrderBy(), schema)};
          entries.emplace_back(cur_sort_key, cur_tuple);
        }
      }
      std::sort(entries.begin(), entries.end(), cmp_);

      /**
       * Output.
       */
      auto new_page_id{to_bpm->NewPage()};
      {
        auto new_page_g{to_bpm->WritePage(new_page_id)};
        auto to_sortpage{new_page_g.AsMut<SortPage>()};
        to_sortpage->Init(schema);
        for (auto &entry : entries) {
          to_sortpage->SerializeAppendData(entry.second);
        }
      }
      to_bpm->DeletePage(need_sort_pages[l_idx_]);
      need_sort_pages[l_idx_] = new_page_id;
      callstack.pop_back();
      continue;
    }
    /**
     * Merge two.
     */
    int32_t mid_idx{l_idx_ + ((r_idx_ - l_idx_) / 2)};

    std::vector<page_id_t> l_page_ids{};
    l_page_ids.resize(mid_idx - l_idx_);
    std::copy(need_sort_pages.begin() + l_idx_, need_sort_pages.begin() + mid_idx, l_page_ids.begin());
    std::vector<page_id_t> r_page_ids{};
    r_page_ids.resize(r_idx_ - mid_idx);
    std::copy(need_sort_pages.begin() + mid_idx, need_sort_pages.begin() + r_idx_, r_page_ids.begin());

    MergeSortRun l_run{std::move(l_page_ids), to_bpm};
    MergeSortRun r_run{std::move(r_page_ids), to_bpm};

    std::vector<page_id_t> new_page_ids{};
    new_page_ids.reserve(r_idx_ - l_idx_);
    for (int32_t i = l_idx_; i < r_idx_; i++) {
      new_page_ids.emplace_back(to_bpm->NewPage());
    }

    {
      int32_t next_new_page_idx{};
      auto new_page_g{to_bpm->WritePage(new_page_ids[next_new_page_idx])};
      auto to_new_sortpage{new_page_g.AsMut<SortPage>()};
      to_new_sortpage->Init(schema);

      auto l_run_iter{l_run.Begin()};
      auto r_run_iter{r_run.Begin()};
      BUSTUB_ASSERT(l_run_iter != l_run.End(), "Unexpected.");
      BUSTUB_ASSERT(r_run_iter != r_run.End(), "Unexpected.");

      auto ensure_new_page_slot_available{[&]() {
        if (to_new_sortpage->IsFull()) {
          next_new_page_idx++;
          BUSTUB_ASSERT(next_new_page_idx < static_cast<int32_t>(new_page_ids.size()), "Unexpected.");
          new_page_g = to_bpm->WritePage(new_page_ids[next_new_page_idx]);
          to_new_sortpage = new_page_g.AsMut<SortPage>();
          to_new_sortpage->Init(schema);
        }
      }};

      SortEntry l_entry{GenerateSortKey(*l_run_iter, to_plan_->GetOrderBy(), schema), *l_run_iter};
      SortEntry r_entry{GenerateSortKey(*r_run_iter, to_plan_->GetOrderBy(), schema), *r_run_iter};

      while (l_run_iter != l_run.End() && r_run_iter != r_run.End()) {
        ensure_new_page_slot_available();
        if (cmp_(l_entry, r_entry)) {
          to_new_sortpage->SerializeAppendData(l_entry.second);
          ++l_run_iter;
          if (l_run_iter != l_run.End()) {
            l_entry = {GenerateSortKey(*l_run_iter, to_plan_->GetOrderBy(), schema), *l_run_iter};
          }
        } else {
          to_new_sortpage->SerializeAppendData(r_entry.second);
          ++r_run_iter;
          if (r_run_iter != r_run.End()) {
            r_entry = {GenerateSortKey(*r_run_iter, to_plan_->GetOrderBy(), schema), *r_run_iter};
          }
        }
      }

      while (l_run_iter != l_run.End()) {
        ensure_new_page_slot_available();
        to_new_sortpage->SerializeAppendData(l_entry.second);
        ++l_run_iter;
        if (l_run_iter != l_run.End()) {
          l_entry = {GenerateSortKey(*l_run_iter, to_plan_->GetOrderBy(), schema), *l_run_iter};
        }
      }

      while (r_run_iter != r_run.End()) {
        ensure_new_page_slot_available();
        to_new_sortpage->SerializeAppendData(r_entry.second);
        ++r_run_iter;
        if (r_run_iter != r_run.End()) {
          r_entry = {GenerateSortKey(*r_run_iter, to_plan_->GetOrderBy(), schema), *r_run_iter};
        }
      }
    }

    /**
     * Merge ended.
     */
    for (int32_t i = l_idx_; i < r_idx_; i++) {
      to_bpm->DeletePage(need_sort_pages[i]);
    }
    for (int32_t i = l_idx_; i < r_idx_; i++) {
      need_sort_pages[i] = new_page_ids[i - l_idx_];
    }
    callstack.pop_back();
  }

  sorted_pages_ = std::move(need_sort_pages);
  sorted_run_ = {sorted_pages_, to_bpm};
  iter_opt_ = sorted_run_.Begin();
  is_built_ = true;
}

/**
 * Yield the next tuple from the external merge sort.
 * @param[out] tuple The next tuple produced by the external merge sort.
 * @param[out] rid The next tuple RID produced by the external merge sort.
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  BUSTUB_ASSERT(iter_opt_.has_value(), "Unexpected.");

  if (iter_opt_ == sorted_run_.End()) {
    return false;
  }

  auto &iter{iter_opt_.value()};
  *tuple = *iter;
  *rid = RID{INVALID_PAGE_ID, 0};
  ++iter;
  return true;
}

template <size_t K>
ExternalMergeSortExecutor<K>::~ExternalMergeSortExecutor() {
  /**
   * 必须先 `Drop` Iterator 持有的 ReadPageGuard.
   */
  iter_opt_ = std::nullopt;
  auto to_bpm{exec_ctx_->GetBufferPoolManager()};
  for (auto &page_id : sorted_pages_) {
    to_bpm->DeletePage(page_id);
  }
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
