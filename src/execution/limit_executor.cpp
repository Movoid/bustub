//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// limit_executor.cpp
//
// Identification: src/execution/limit_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/limit_executor.h"
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new LimitExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The limit plan to be executed
 * @param child_executor The child executor from which limited tuples are pulled
 */
LimitExecutor::LimitExecutor(ExecutorContext *exec_ctx, const LimitPlanNode *plan,
                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the limit */
void LimitExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
  to_child_executor_->Init();
  count_ = 0;
}

/**
 * Yield the next tuple from the limit.
 * @param[out] tuple The next tuple produced by the limit
 * @param[out] rid The next tuple RID produced by the limit
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto LimitExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
  if (count_ == plan_->GetLimit()) {
    return false;
  }

  Tuple cur_tuple{};
  RID cur_rid{};

  auto status{to_child_executor_->Next(&cur_tuple, &cur_rid)};
  if (!status) {
    return false;
  }

  *tuple = cur_tuple;
  *rid = cur_rid;
  count_++;
  return true;
}

}  // namespace bustub
