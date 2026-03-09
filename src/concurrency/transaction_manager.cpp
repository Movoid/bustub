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
   * 待完成.
   */

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
 */
void TransactionManager::Abort(Transaction *txn) {
  if (txn->state_ != TransactionState::RUNNING && txn->state_ != TransactionState::TAINTED) {
    throw Exception("txn not in running / tainted state");
  }

  // TODO(fall2023): Implement the abort logic!

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
  txn->state_ = TransactionState::ABORTED;
  running_txns_.RemoveTxn(txn->read_ts_);
}

/** @brief Stop-the-world garbage collection. Will be called only when all transactions are not accessing the table
 * heap. */
void TransactionManager::GarbageCollection() { UNIMPLEMENTED("not implemented"); }

}  // namespace bustub
