//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "execution/execution_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Begins a new transaction.
 * @param isolation_level an optional isolation level of the transaction.
 * @return an initialized transaction
 */
auto TransactionManager::Begin(IsolationLevel isolation_level) -> Transaction * {
  std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
  auto txn_id = next_txn_id_.fetch_add(1, std::memory_order_relaxed);
  auto to_txn_uniq = std::make_unique<Transaction>(txn_id, isolation_level);
  auto *to_txn = to_txn_uniq.get();
  txn_map_.insert(std::make_pair(txn_id, std::move(to_txn_uniq)));

  auto start_ts{last_commit_ts_.load(std::memory_order_relaxed)};
  to_txn->read_ts_.store(start_ts, std::memory_order_relaxed);
  running_txns_.AddTxn(to_txn->read_ts_);

  return to_txn;
}

/** @brief Verify if a txn satisfies serializability. We will not test this function and you can change / remove it as
 * you want. */
auto TransactionManager::VerifyTxn(Transaction *txn) -> bool { return true; }

/**
 * Commits a transaction.
 * @param txn the transaction to commit, the txn will be managed by the txn manager so no need to delete it by
 * yourself
 */
auto TransactionManager::Commit(Transaction *to_txn) -> bool {
  std::unique_lock<std::mutex> commit_lck(commit_mutex_);

  // TODO(fall2023): acquire commit ts!
  auto commit_ts{last_commit_ts_.load(std::memory_order_relaxed) + 1};

  if (to_txn->state_ != TransactionState::RUNNING) {
    throw Exception("txn not in running state");
  }

  if (to_txn->GetIsolationLevel() == IsolationLevel::SERIALIZABLE) {
    if (!VerifyTxn(to_txn)) {
      commit_lck.unlock();
      Abort(to_txn);
      return false;
    }
  }

  // TODO(fall2023): Implement the commit logic!
  /**
   * 通过 WriteSet 追踪被修改的元组,
   * 修改 timestamp.
   */
  for (const auto &table_write_set : to_txn->GetWriteSets()) {
    auto table_oid{table_write_set.first};
    auto &to_table{catalog_->GetTable(table_oid)->table_};
    for (const auto &rid : table_write_set.second) {
      auto tuple_meta{to_table->GetTupleMeta(rid)};
      BUSTUB_ASSERT(tuple_meta.ts_ == to_txn->GetTransactionTempTs(), "Unexpected.");
      tuple_meta.ts_ = commit_ts;
      to_table->UpdateTupleMeta(tuple_meta, rid);
    }
  }

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);

  to_txn->commit_ts_.store(commit_ts, std::memory_order_relaxed);
  auto old_commit_ts{last_commit_ts_.fetch_add(1, std::memory_order_relaxed)};
  BUSTUB_ASSERT(old_commit_ts + 1 == commit_ts, "Unexpected.");

  to_txn->state_ = TransactionState::COMMITTED;
  running_txns_.UpdateCommitTs(to_txn->commit_ts_);
  running_txns_.RemoveTxn(to_txn->read_ts_);

  return true;
}

/**
 * Aborts a transaction
 * @param txn the transaction to abort, the txn will be managed by the txn manager so no need to delete it by yourself
 *
 * WriteSet 记录了当前 Txn 操作过的 Tuple 位置.
 * 由于 temp ts 的存在, 当前事务独占了对此 Tuple 位置的修改权.
 */
void TransactionManager::Abort(Transaction *to_txn) {
  if (to_txn->state_ != TransactionState::RUNNING && to_txn->state_ != TransactionState::TAINTED) {
    throw Exception("txn not in running / tainted state");
  }

  for (const auto &[table_oid, table_rid_set] : to_txn->write_set_) {
    for (const auto &rid : table_rid_set) {
      const auto *to_table_info{catalog_->GetTable(table_oid).get()};
      auto to_table{to_table_info->table_.get()};
      const auto &table_schema{to_table_info->schema_};

      auto old_meta{to_table->GetTupleMeta(rid)};

      if ((old_meta.ts_ & TXN_START_ID) == 0) {
        continue;
      }
      BUSTUB_ASSERT(old_meta.ts_ == to_txn->GetTransactionTempTs(), "Unexpected.");

      /**
       * 通常不可能出现脏写.
       * 当前事务独占此 RID 位置上的修改权.
       */
      auto undo_link_opt{GetUndoLink(rid)};
      /**
       * 若事务执行的是 Insert 操作,
       * 可能没有 UndoLink.
       */
      if (undo_link_opt.has_value()) {
        auto undo_link{undo_link_opt.value()};
        BUSTUB_ASSERT(undo_link.prev_txn_ == to_txn->GetTransactionId(), "Unexpected.");

        auto undo_log{GetUndoLog(undo_link)};
        BUSTUB_ASSERT(undo_log.prev_version_.prev_txn_ != to_txn->GetTransactionId(), "Unexpected.");

        auto [old_meta, old_tuple]{to_table->GetTuple(rid)};
        auto recov_tuple_opt{ReconstructTuple(&table_schema, old_tuple, old_meta, {undo_log})};

        TupleMeta recov_meta{undo_log.ts_, !recov_tuple_opt.has_value()};
        UpdateTupleAndUndoLink(this, rid, undo_log.prev_version_, to_table, to_txn, recov_meta,
                               recov_tuple_opt.value_or(old_tuple));

      } else {
        TupleMeta recov_meta{0, true};
        to_table->UpdateTupleMeta(recov_meta, rid);
      }
    }
  }

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
  to_txn->state_ = TransactionState::ABORTED;
  running_txns_.RemoveTxn(to_txn->read_ts_);
}

/**
 * @brief Stop-the-world garbage collection.
 * Will be called only when all transactions are not accessing the table heap.
 *
 * user 保证所有事务暂停时, 通过命令 `\txn gc` 调用 GC.
 * 若一个 Tuple 有版本的 ts <= watermark, 则这个版本之前的所有版本可被回收.
 */
void TransactionManager::GarbageCollection() {
  // UNIMPLEMENTED("not implemented");
  auto watermark{GetWatermark()};

  /**
   * 遍历所有 Table.
   * 对存在仍可被访问的 Txn 进行标记.
   * 若 Txn 没有可被访问的日志, 可直接从 TxnMap 删除.
   * 不需要调整 UndoLink 断开链表, 正确的版本回溯不会解引用无效的 UndoLink.
   */

  std::unordered_map<txn_id_t, bool> cannot_delete{};

  auto table_names{catalog_->GetTableNames()};

  for (const auto &table_name : table_names) {
    auto to_table_info{catalog_->GetTable(table_name)};
    auto &to_table{to_table_info->table_};

    auto table_iter{to_table->MakeEagerIterator()};

    while (!table_iter.IsEnd()) {
      auto cur_rid{table_iter.GetRID()};
      /**
       * Check if have UndoLog.
       */
      BUSTUB_ASSERT(cur_rid.GetPageId() != INVALID_PAGE_ID, "Unexpected.");
      auto page_version_info_iter{version_info_.find(cur_rid.GetPageId())};
      if (page_version_info_iter == version_info_.end()) {
        ++table_iter;
        continue;
      }
      auto &page_version_info{page_version_info_iter->second};

      auto prev_link_iter{page_version_info->prev_link_.find(cur_rid.GetSlotNum())};
      if (prev_link_iter == page_version_info->prev_link_.end()) {
        ++table_iter;
        continue;
      }

      /**
       * Start GC.
       */
      auto cur_undo_link{prev_link_iter->second};
      auto [cur_tuple, cur_meta]{table_iter.GetTuple()};
      if (cur_tuple.ts_ <= watermark) {
        ++table_iter;
        continue;
      }

      while (cur_undo_link.IsValid()) {
        BUSTUB_ASSERT(txn_map_.count(cur_undo_link.prev_txn_), "Unexpected.");
        const auto &cur_undo_log{txn_map_[cur_undo_link.prev_txn_]->undo_logs_[cur_undo_link.prev_log_idx_]};
        cannot_delete[cur_undo_link.prev_txn_] = true;
        if (cur_undo_log.ts_ <= watermark) {
          break;
        }
        /**
         * 应该至少有一个 UndoLog `ts_ <= watermark`,
         * 此处前往的 UndoLog 应该仍存在.
         * 若其已经是 `watermark` 之下的唯一一个 UndoLog,
         * 则会先 `break`.
         */
        cur_undo_link = cur_undo_log.prev_version_;
      }

      ++table_iter;
    }
  }

  for (auto txn_map_iter = txn_map_.begin(); txn_map_iter != txn_map_.end();) {
    if (cannot_delete[txn_map_iter->first]) {
      txn_map_iter++;
      continue;
    }
    auto txn_state{txn_map_iter->second->GetTransactionState()};
    if (txn_state == TransactionState::RUNNING || txn_state == TransactionState::TAINTED) {
      txn_map_iter++;
      continue;
    }
    txn_map_iter = txn_map_.erase(txn_map_iter);
  }
}

}  // namespace bustub
