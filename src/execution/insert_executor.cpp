//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the insert */
void InsertExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  inserted_cnt_ = 0;
  is_consumed_ = false;

  to_child_executor_->Init();
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple The integer tuple indicating the number of rows inserted into the table
 * @param[out] rid The next tuple RID produced by the insert (ignore, not used)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with number of inserted rows produced only once.
 *
 * 类似 UpdateExecutor, 一直执行,
 * 最后输出插入元组数量.
 */
auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  auto to_txn_mgr{exec_ctx_->GetTransactionManager()};
  auto to_txn{exec_ctx_->GetTransaction()};

  const auto all_to_index_infos{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};
  auto to_table{to_table_info_->table_.get()};
  const auto &table_schema{to_table_info_->schema_};

  BUSTUB_ASSERT(table_schema.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
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

  /**
   * Collect.
   */
  Tuple cur_tuple{};
  RID cur_rid{};

  if (all_to_index_infos.empty()) {
    /**
     * Table 没有 Index.
     * 不可能 IndexScan.
     * 始终直接插入新的 Tuple.
     */
    while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
      TupleMeta insert_meta{to_txn->GetTransactionTempTs(), false};
      auto insert_rid_opt{to_table->InsertTuple(insert_meta, cur_tuple)};
      BUSTUB_ASSERT(insert_rid_opt.has_value(), "Unexepcted fail.");
      auto insert_rid{insert_rid_opt.value()};
      to_txn->AppendWriteSet(to_table_info_->oid_, insert_rid);
      inserted_cnt_++;
    }

  } else {
    /**
     * 可能使用 IndexScan.
     * 注意 Halloween Problem.
     */
    std::vector<Tuple> input_tuples{};
    while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
      input_tuples.emplace_back(std::move(cur_tuple));
    }

    if (has_primary_index) {
      /**
       * 需要判断是否复用 RID.
       *
       * WARNING: 对主键 Index 的操作必须在最后一步.
       * 主键 Index Entry 插入成功, 则事务 **不允许失败**.
       * 否则此 key 对应的 RID 上, Tuple 持有 temp ts,
       * 会阻止其他任何事务在此 key 对应位置的操作.
       */
      std::vector<RID> existed_rid{};
      existed_rid.reserve(1);
      for (const auto &input_tuple : input_tuples) {
        auto primary_key_tuple{input_tuple.KeyFromTuple(table_schema, to_primary_index_info->key_schema_,
                                                        to_primary_index->GetKeyAttrs())};
        to_primary_index->ScanKey(primary_key_tuple, &existed_rid, to_txn);
        BUSTUB_ASSERT(existed_rid.size() <= 1, "Unexpected.");

        if (existed_rid.empty()) {
          /**
           * 没有可复用的 RID.
           * 需要在 TableHeap 上插入新的 Tuple.
           * 最后需要插入新的主键 Index Entry, 可能失败,
           *
           * > 若插入主键 Index Entry 失败, 用户负责重启 Txn,
           * > 被加入 TableHeap 的元组持有 temp ts 且 UndoLog 为空, 应该不会被使用.
           * > 重启后新的 Txn 应该会找到发生冲突的 RID 并尝试复用.
           */
          TupleMeta insert_meta{to_txn->GetTransactionTempTs(), false};
          auto insert_rid_opt{to_table->InsertTuple(insert_meta, input_tuple)};
          BUSTUB_ASSERT(insert_rid_opt.has_value(), "Unexpected.");
          auto insert_rid{insert_rid_opt.value()};
          to_txn->AppendWriteSet(to_table_info_->oid_, insert_rid);

          for (auto &to_index_info : all_to_index_infos) {
            if (to_index_info->is_primary_key_) {
              continue;
            }
            BUSTUB_ASSERT(false, "Unsupport.");
            auto &to_index{to_index_info->index_};
            auto insert_key_tuple{
                input_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
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

          auto primary_key_tuple{input_tuple.KeyFromTuple(table_schema, to_primary_index_info->key_schema_,
                                                          to_primary_index->GetKeyAttrs())};
          auto status{to_primary_index->InsertEntry(primary_key_tuple, insert_rid, to_txn)};
          if (!status) {
            to_txn->SetTainted();
            throw ExecutionException{"Another txn inserted a primary key index entry earlier."};
          }

        } else {
          /**
           * 复用 RID, 尝试添加新版本.
           * 不改变 Index Entry.
           *
           * 直接尽可能读取最新版本.
           * Insert 操作读取旧版本没有意义.
           *
           * WARNING: 重复进行 Insert 操作应该被视为错误,
           * 而不是更新.
           */
          auto update_rid{existed_rid[0]};

          for (auto &to_index_info : all_to_index_infos) {
            if (to_index_info->is_primary_key_) {
              continue;
            }
            BUSTUB_ASSERT(false, "Unsupport.");
            auto &to_index{to_index_info->index_};
            auto insert_key_tuple{
                input_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
            /**
             * WARNING: 实际不支持 Second Index.
             * 若一个 Tuple 被删除, 其存在 Second Index 的 Column 取值无法被重新使用.
             * 尝试插入另一个在此 Column 上取值相同的 Tuple, 此处 InsertEntry 会失败.
             */
            auto status{to_index->InsertEntry(insert_key_tuple, update_rid, to_txn)};
            if (!status) {
              to_txn->SetTainted();
              throw ExecutionException{"Conflict on second index."};
            }
          }

          auto [old_meta, old_tuple, undo_link_opt]{GetTupleAndUndoLink(to_txn_mgr, to_table, update_rid)};

          /**
           * `base_tuple` 使用 `nullptr`.
           * 这个 Tuple 的最新状态必须是 deleted.
           */
          TryUpdateTupleAndUndoLinkWithCheck(to_txn_mgr, to_txn, to_table_info_, update_rid, &old_meta, nullptr,
                                             &input_tuple, true);

          to_txn->AppendWriteSet(to_table_info_->oid_, update_rid);
        }

        inserted_cnt_++;
        existed_rid.clear();
      }
    } else {
      /**
       * 没有主键索引.
       * 一般来讲, 所有其他索引的 Index Entry 的 key 允许出现重复,
       * 没有任何重复元组检查.
       *
       * 目前 Index 实现为不允许 key 重复.
       */
      for (const auto &input_tuple : input_tuples) {
        TupleMeta insert_meta{to_txn->GetTransactionTempTs(), false};
        auto insert_rid_opt{to_table->InsertTuple(insert_meta, input_tuple)};
        BUSTUB_ASSERT(insert_rid_opt.has_value(), "Unexepcted fail.");
        auto insert_rid{insert_rid_opt.value()};
        to_txn->AppendWriteSet(to_table_info_->oid_, insert_rid);
        for (auto &to_index_info : all_to_index_infos) {
          BUSTUB_ASSERT(false, "Unsupported.");
          auto &to_index{to_index_info->index_};
          auto insert_key_tuple{
              input_tuple.KeyFromTuple(table_schema, to_index_info->key_schema_, to_index->GetKeyAttrs())};
          auto status{to_index->InsertEntry(insert_key_tuple, insert_rid, to_txn)};
          if (!status) {
            to_txn->SetTainted();
            throw ExecutionException{"Conflict on second index."};
          }
        }
        inserted_cnt_++;
      }
    }
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, inserted_cnt_);
  *tuple = Tuple{std::move(output_values), &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
