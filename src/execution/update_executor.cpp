//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/update_executor.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 *
 * Update Executor 只有一个子 Executor 作为输入.
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the update */
void UpdateExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
  /**
   * 记得先 Init 子 Executor.
   */
  to_child_executor_->Init();
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
}

/**
 * Yield the next tuple from the update.
 * @param[out] tuple The next tuple produced by the update
 * @param[out] rid The next tuple RID produced by the update (ignore this)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid` out-parameter.
 *
 * Update 操作需要同时更新 Index.
 *
 * OutputSchema 中只有 1 Column, 1 Row, 类型 bustub::INTEGER,
 * 名字为 "__bustub_internal.update_rows".
 * Update Executor 调用 `Next` 后需要一直执行直到处理完所有输入,
 * 并输出这唯一一行计数.
 *
 * 这个计数表不需要 RID.
 */
auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  const auto to_all_index_info{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};

  /**
   * 下层的 Output Schema 必须和 Table Schema 相同.
   * 因为 `Index::GetKeyAttrs()` 返回的始终是基于 Table Schema 的 Column 下标.
   */
  BUSTUB_ASSERT(to_table_info_->schema_.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
                "Unexpected Schema.");

  /**
   * 由于 bustub 的 Iterator 设计, 一边 Scan 一边 Update 不会有 Halloween 问题.
   * Update 实现为先 Delete 再 Insert.
   */
  Tuple cur_tuple{};
  RID cur_rid{};
  std::vector<Value> insert_values{};
  insert_values.reserve(to_table_info_->schema_.GetColumnCount());
  while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
    /**
     * 删除旧 Tuple.
     */
    auto &to_table{to_table_info_->table_};
    auto old_meta{to_table->GetTupleMeta(cur_rid)};
    old_meta.is_deleted_ = true;
    to_table->UpdateTupleMeta(old_meta, cur_rid);
    /**
     * 计算新 Tuple.
     */
    insert_values.clear();
    for (auto &to_expr : to_plan_->target_expressions_) {
      insert_values.emplace_back(to_expr->Evaluate(&cur_tuple, to_table_info_->schema_));
    }
    /**
     * 插入新 Tuple.
     */
    TupleMeta insert_meta{0, false};  // WARNING: SHOULD BE FIXED IN THE FUTURE.
    Tuple insert_tuple{insert_values, &to_table_info_->schema_};
    auto insert_rid_opt{to_table->InsertTuple(insert_meta, insert_tuple)};
    BUSTUB_ASSERT(insert_rid_opt.has_value(), "Invalid RID.");
    BUSTUB_ASSERT(insert_rid_opt->GetPageId(), "Invalid RID.");
    auto insert_rid{insert_rid_opt.value()};
    /**
     * 对所有 Index, 删除旧 Index Entry, 插入新 Index Entry.
     */
    for (auto &to_index_info : to_all_index_info) {
      auto &cur_index{to_index_info->index_};
      /**
       * 注意, 需要传递 key 的 Tuple 而不是完整 Tuple.
       */
      auto cur_key_tuple{
          cur_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
      cur_index->DeleteEntry(cur_key_tuple, cur_rid, exec_ctx_->GetTransaction());

      auto insert_key_tuple{
          insert_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
      BUSTUB_ASSERT(insert_key_tuple.GetLength() <= 8, "Unsupported key.");
      auto status{cur_index->InsertEntry(insert_key_tuple, insert_rid, exec_ctx_->GetTransaction())};
      BUSTUB_ASSERT(status, "Unexpected fail.");
    }
    updated_cnt_++;
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, updated_cnt_);
  *tuple = Tuple{output_values, &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
