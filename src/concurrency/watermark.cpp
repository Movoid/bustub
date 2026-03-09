//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// watermark.cpp
//
// Identification: src/concurrency/watermark.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/watermark.h"
#include <exception>
#include "common/exception.h"

namespace bustub {

auto Watermark::AddTxn(timestamp_t read_ts) -> void {
  if (read_ts < commit_ts_) {
    throw Exception("read ts < commit ts");
  }
  active_txn_count_on_readts_[read_ts]++;

  while (active_txn_count_on_readts_.count(watermark_) == 0) {
    watermark_++;
  }
}

auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
  auto iter{active_txn_count_on_readts_.find(read_ts)};
  BUSTUB_ASSERT(iter != active_txn_count_on_readts_.end(), "Unexpected.");
  iter->second--;

  if (iter->second != 0) {
    return;
  }
  auto cur_ts{iter->first};
  active_txn_count_on_readts_.erase(iter);

  if (active_txn_count_on_readts_.empty()) {
    watermark_ = commit_ts_;
    return;
  }

  if (watermark_ == cur_ts) {
    while (active_txn_count_on_readts_.count(watermark_) == 0) {
      watermark_++;
    }
  }
}

}  // namespace bustub
