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
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

#include "execution/executors/update_executor.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 *
 * Update Executor 只有一个子 Executor 作为输入.
 *
 * 关于 MVCC 支持,
 * UpdateExecutor 需要在 RID 位置上原地修改.
 * 若实现为 Delete + Insert, 若由于某 index key 改变, 使得插入新的 index entry,
 * 若 Child Executor 是 Index Scan, 会造成 Halloween problem.
 * 必须实现为先收集 RID 再修改.
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Initialize the update
 */
void UpdateExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  updated_cnt_ = 0;
  is_consumed_ = false;

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

  auto to_txn_mgr{exec_ctx_->GetTransactionManager()};
  auto to_txn{exec_ctx_->GetTransaction()};

  const auto all_to_index_infos{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};
  auto to_table{to_table_info_->table_.get()};
  const auto &table_schema{to_table_info_->schema_};

  /**
   * 下层的 Output Schema 必须和 Table Schema 相同.
   * 因为 `Index::GetKeyAttrs()` 返回的始终是基于 Table Schema 的 Column 下标.
   */
  BUSTUB_ASSERT(to_table_info_->schema_.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
                "Unexpected Schema.");

  bool has_primary_index{};
  IndexInfo *to_primary_index_info{};
  Index *to_primary_index{};
  for (const auto &to_index_info : all_to_index_infos) {
    if (to_index_info->is_primary_key_) {
      has_primary_index = true;
      to_primary_index_info = to_index_info.get();
      to_primary_index = to_index_info->index_.get();
      break;
    }
  }

  auto calc_new_tuple{[&](const Tuple &old_tuple) -> Tuple {
    std::vector<Value> new_values{};
    new_values.reserve(table_schema.GetColumnCount());
    for (auto &to_expr : to_plan_->target_expressions_) {
      new_values.emplace_back(to_expr->Evaluate(&old_tuple, table_schema));
    }
    return Tuple{std::move(new_values), &table_schema};
  }};

  Tuple cur_tuple{};
  RID cur_rid{};

  if (all_to_index_infos.empty()) {
    /**
     * Table 没有 Index.
     * 直接尝试原地更新.
     */
    while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
      auto old_meta{to_table->GetTupleMeta(cur_rid)};
      auto new_tuple{calc_new_tuple(cur_tuple)};
      TryUpdateTupleAndUndoLinkWithCheck(to_txn_mgr, to_txn, to_table_info_, cur_rid, &old_meta, &cur_tuple, &new_tuple,
                                         false);
      auto update_rid{cur_tuple.GetRid()};
      to_txn->AppendWriteSet(to_table_info_->oid_, update_rid);
      updated_cnt_++;
    }
  } else {
    /**
     * 可能使用 IndexScan.
     */
    std::vector<Tuple> input_tuples{};
    while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
      input_tuples.emplace_back(std::move(cur_tuple));
    }

    if (has_primary_index) {
      /**
       * 有主键索引.
       * 既然这个 Tuple 能被子 Executor 读取到,
       * 则其在主键 Index 一定存在 Index Entry.
       *
       * 主键若发生变化则需要走 Insert 逻辑.
       */

      std::vector<RID> existed_rid{};
      existed_rid.reserve(1);
      for (const auto &input_tuple : input_tuples) {
        auto new_tuple{calc_new_tuple(input_tuple)};
        auto old_primary_key_tuple{input_tuple.KeyFromTuple(table_schema, to_primary_index_info->key_schema_,
                                                            to_primary_index->GetKeyAttrs())};
        auto new_primary_key_tuple{
            new_tuple.KeyFromTuple(table_schema, to_primary_index_info->key_schema_, to_primary_index->GetKeyAttrs())};

        if (IsTupleContentEqual(old_primary_key_tuple, new_primary_key_tuple)) {
          auto update_rid{input_tuple.GetRid()};

          for (auto &to_index_info : all_to_index_infos) {
            if (to_index_info->is_primary_key_) {
              continue;
            }
            BUSTUB_ASSERT(false, "Unsupport.");
            auto &to_index{to_index_info->index_};
            auto old_key_tuple{
                input_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
            auto new_key_tuple{
                new_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
            if (IsTupleContentEqual(old_key_tuple, new_key_tuple)) {
              continue;
            }
            /**
             * WARNING: 实际不支持 Second Index.
             * 若一个 Tuple 被删除, 其存在 Second Index 的 Column 取值无法被重新使用.
             * 尝试插入另一个在此 Column 上取值相同的 Tuple, 此处 InsertEntry 会失败.
             */
            auto status{to_index->InsertEntry(new_key_tuple, update_rid, to_txn)};
            if (!status) {
              to_txn->SetTainted();
              throw ExecutionException{"Conflict on second index."};
            }
          }

          auto old_meta{to_table->GetTupleMeta(update_rid)};
          TryUpdateTupleAndUndoLinkWithCheck(to_txn_mgr, to_txn, to_table_info_, update_rid, &old_meta, &input_tuple,
                                             &new_tuple, false);
          to_txn->AppendWriteSet(to_table_info_->oid_, update_rid);
          continue;
        }

        /**
         * 主键改变.
         * 需要进行 Insert 逻辑, 插入更新后的元组.
         * 若更新后的主键和表中另一元组重复, 则插入 Index Entry 将失败.
         */

        TupleMeta insert_meta{to_txn->GetTransactionTempTs(), false};
        auto insert_rid_opt{to_table->InsertTuple(insert_meta, new_tuple)};
        BUSTUB_ASSERT(insert_rid_opt.has_value(), "Unexpected.");
        auto insert_rid{insert_rid_opt.value()};

        for (auto &to_index_info : all_to_index_infos) {
          if (to_index_info->is_primary_key_) {
            continue;
          }
          BUSTUB_ASSERT(false, "Unsupport.");
          auto &to_index{to_index_info->index_};
          auto insert_key_tuple{
              new_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
          /**
           * WARNING: 实际不支持 Second Index.
           * 若一个 Tuple 被删除, 其存在 Second Index 的 Column 取值无法被重新使用.
           * 尝试插入另一个在此 Column 上取值相同的 Tuple, 此处 InsertEntry 会失败.
           */
          auto status{to_index->InsertEntry(insert_key_tuple, insert_rid, to_txn)};
          if (!status) {
            to_txn->SetTainted();
            throw ExecutionException{"Conflict on second index."};
          }
        }

        auto status{to_primary_index->InsertEntry(new_primary_key_tuple, insert_rid, to_txn)};
        if (!status) {
          to_txn->SetTainted();
          throw ExecutionException{"Conflict on primary key index with another tuple."};
        }

        to_txn->AppendWriteSet(to_table_info_->oid_, insert_rid);
      }

      updated_cnt_++;

    } else {
      /**
       * 没有主键索引.
       * 但有其他 Index.
       */
      for (const auto &input_tuple : input_tuples) {
        auto new_tuple{calc_new_tuple(input_tuple)};

        auto update_rid{new_tuple.GetRid()};

        auto old_meta{to_table->GetTupleMeta(update_rid)};
        TryUpdateTupleAndUndoLinkWithCheck(to_txn_mgr, to_txn, to_table_info_, update_rid, &old_meta, &input_tuple,
                                           &new_tuple, false);

        for (auto &to_index_info : all_to_index_infos) {
          BUSTUB_ASSERT(false, "Unsupported.");
          auto &to_index{to_index_info->index_};
          auto old_key_tuple{
              input_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
          auto new_key_tuple{new_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};

          if (IsTupleContentEqual(old_key_tuple, new_key_tuple)) {
            continue;
          }

          auto status{to_index->InsertEntry(new_key_tuple, update_rid, to_txn)};
          if (!status) {
            to_txn->SetTainted();
            throw ExecutionException{"Conflict on second index."};
          }
        }
        updated_cnt_++;
      }
    }
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, updated_cnt_);
  *tuple = Tuple{std::move(output_values), &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
