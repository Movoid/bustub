//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); };

 private:
  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *to_plan_;

  std::unique_ptr<AbstractExecutor> to_left_executor_;
  std::unique_ptr<AbstractExecutor> to_right_executor_;

  Tuple cur_left_tuple_;
  RID cur_left_rid_{INVALID_PAGE_ID, 0};
  hash_t cur_left_key_hash_{};
  std::vector<Value> cur_left_key_vals_;
  bool cur_left_tuple_is_joined_{};

  std::unordered_map<int64_t, std::vector<Tuple>> right_ht_;
  int32_t next_rid_pos_{};  // for bucket of `right_ht_`.

  bool is_consumed_{};
};

}  // namespace bustub
