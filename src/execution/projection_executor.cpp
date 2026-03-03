//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// projection_executor.cpp
//
// Identification: src/execution/projection_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/projection_executor.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Construct a new ProjectionExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The projection plan to be executed
 */

/**
 * Projection 从下层的 **唯一一个算子** 即 `child_executor` 获得输入的元组.
 * 使用 `AbstractExecutor` parent class.
 */
ProjectionExecutor::ProjectionExecutor(ExecutorContext *exec_ctx, const ProjectionPlanNode *plan,
                                       std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the projection */
void ProjectionExecutor::Init() {
  // Initialize the child executor
  child_executor_->Init();
}

/**
 * Yield the next tuple from the projection.
 * @param[out] tuple The next tuple produced by the projection
 * @param[out] rid The next tuple RID produced by the projection
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
/**
 * 注意 Volcano 执行模型, Next 一次只处理 **一个元组**.
 */
auto ProjectionExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple child_tuple{};

  // Get the next tuple
  const auto status = child_executor_->Next(&child_tuple, rid);

  if (!status) {
    return false;
  }

  // Compute expressions
  /**
   * Projection 对每个输入的 Tuple,
   * 执行用户通过 SQL 指定的 Expression, 每个 Expression 对应结果 Tuple 中一个 Column 的取值.
   * 将每个 Expression 求值得到的 Value 拼接, 得到结果 Tuple.
   */
  std::vector<Value> values{};
  values.reserve(GetOutputSchema().GetColumnCount());
  for (const auto &expr : plan_->GetExpressions()) {
    values.push_back(expr->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
  }

  // 返回提供的 tuple.
  *tuple = Tuple{values, &GetOutputSchema()};

  return true;
}
}  // namespace bustub
