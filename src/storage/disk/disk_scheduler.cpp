
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

// #define CUSTOM_LOG
// #include "common/custom_debug.h"

#include "storage/disk/disk_scheduler.h"
#include <cstring>
#include <utility>
#include "common/macros.h"
#include "fmt/base.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

// ========================================
//
// ConcurrentTempFrameBuffer
//
// ========================================
ConcurrentTempFrameBuffer::ConcurrentTempFrameBuffer(size_t base_frame_cnt) : base_frame_cnt_{base_frame_cnt} {
  base_ = new (std::align_val_t{simple_cu::utils::CACHELINE_SIZE}) char[base_frame_cnt * BUSTUB_PAGE_SIZE];
  for (size_t i = 0; i < base_frame_cnt; i++) {
    available_frames_.Push(base_ + (i * BUSTUB_PAGE_SIZE));
  }
}

ConcurrentTempFrameBuffer::~ConcurrentTempFrameBuffer() {
  ::operator delete[](base_, std::align_val_t{simple_cu::utils::CACHELINE_SIZE});
}

auto ConcurrentTempFrameBuffer::Allocate() -> char * { return available_frames_.Pop(); }

auto ConcurrentTempFrameBuffer::TryAllocate(char *&res) -> bool { return available_frames_.TryPop(res); }

void ConcurrentTempFrameBuffer::Deallocate(char *ptr) { available_frames_.Push(ptr); }

auto ConcurrentTempFrameBuffer::IsDeallocatable(const char *ptr) -> bool {
  return base_ <= ptr && ptr < base_ + (base_frame_cnt_ * BUSTUB_PAGE_SIZE);
}

auto ConcurrentTempFrameBuffer::TryDeallocate(char *ptr) -> bool {
  if (!IsDeallocatable(ptr)) {
    return false;
  }
  Deallocate(ptr);
  return true;
};

// ========================================
//
// DiskScheduler::PageTaskBlock
//
// ========================================
DiskScheduler::PageTaskBlock::PageTaskBlock(page_id_t page_id, WorkerTable::WorkerShard *to_worker_shard)
    : ptb_page_id_{page_id}, to_worker_shard_{to_worker_shard} {}

void DiskScheduler::PageTaskBlock::SubmitFlushTask(char *new_flush_src, bool is_deallocable,
                                                   DiskSchedulerPromise &&callback) {
  auto old_state{state_.load(std::memory_order_acquire)};

  pending_flush_cb_.emplace(Callback{std::move(callback), new_flush_src});
  auto old_flush_src{flush_src_.exchange(new_flush_src, std::memory_order_release)};

  while (true) {
    auto new_state{old_state | State::PENDING_FLUSH | State::IS_INQUEUE};
    if (state_.compare_exchange_weak(old_state, new_state, std::memory_order_release, std::memory_order_relaxed)) {
      if ((old_state & State::IS_INQUEUE) == 0) {
        to_worker_shard_->pending_queue_.Push(this);
      }
      break;
    }
  }

  to_worker_shard_->temp_frame_buffer_.TryDeallocate(old_flush_src);
}

void DiskScheduler::PageTaskBlock::SubmitFetchTask(char *new_fetch_dst, bool is_deallocable,
                                                   DiskSchedulerPromise &&callback) {
  auto old_state{state_.load(std::memory_order_acquire)};

  pending_fetch_cb_.emplace(Callback{std::move(callback), new_fetch_dst});
  auto old_fetch_dst{fetch_dst_.exchange(new_fetch_dst, std::memory_order_release)};

  while (true) {
    auto new_state{old_state | State::PENDING_FETCH | State::IS_INQUEUE};
    if (state_.compare_exchange_weak(old_state, new_state, std::memory_order_release, std::memory_order_relaxed)) {
      if ((old_state & State::IS_INQUEUE) == 0) {
        to_worker_shard_->pending_queue_.Push(this);
      }
      break;
    }
  }

  to_worker_shard_->temp_frame_buffer_.TryDeallocate(old_fetch_dst);
}

auto DiskScheduler::PageTaskBlock::GatherFlushTask(std::vector<DiskSchedulerPromise> &flush_cb) -> char * {
  char *ret_flush_src{};
  while (true) {
    /**
     * 仅 unset `PENDING_FLUSH`.
     * 若末尾 `PENDING_FLUSH` 被再次设置, 则说明有新的 Flush 到来.
     */
    auto old_state{state_.fetch_and(~State::PENDING_FLUSH, std::memory_order_acquire)};

    char *gathered_flush_src{flush_src_.exchange(nullptr, std::memory_order_acquire)};
    if (gathered_flush_src == nullptr) {
      BUSTUB_ASSERT(ret_flush_src != nullptr || flush_cb.empty(), "Unexpected.");
      return ret_flush_src;
    }

    // `gathered_flush_src != nullptr`.
    if (ret_flush_src != nullptr) {
      to_worker_shard_->temp_frame_buffer_.TryDeallocate(ret_flush_src);
    }
    ret_flush_src = gathered_flush_src;

    Callback cur_cb{};
    while (true) {
      bool check{pending_flush_cb_.try_dequeue(cur_cb)};
      BUSTUB_ASSERT(check, "Unexpected.");
      if (cur_cb.callback_opt_.has_value()) {
        flush_cb.emplace_back(std::move(cur_cb.callback_opt_.value()));
      }
      if (cur_cb.validation_ptr_ == ret_flush_src) {
        break;
      }
    }

    old_state = state_.load(std::memory_order_relaxed);
    if ((old_state & State::PENDING_FLUSH) == 0) {
      break;
    }
    /**
     * `state_` 被再次置位 `PENDING_FLUSH`.
     */
  }
  BUSTUB_ASSERT(ret_flush_src != nullptr || flush_cb.empty(), "Unexpected.");
  return ret_flush_src;
}

auto DiskScheduler::PageTaskBlock::GatherFetchTask(std::vector<DiskSchedulerPromise> &fetch_cb) -> char * {
  char *ret_fetch_dst{};
  while (true) {
    /**
     * 仅 unset `PENDING_FETCH`.
     * 若末尾 `PENDING_FETCH` 被再次设置, 则说明有新的 Fetch 到来.
     */
    auto old_state{state_.fetch_and(~State::PENDING_FETCH, std::memory_order_acquire)};

    char *gathered_fetch_dst{fetch_dst_.exchange(nullptr, std::memory_order_acquire)};
    if (gathered_fetch_dst == nullptr) {
      BUSTUB_ASSERT(ret_fetch_dst != nullptr || fetch_cb.empty(), "Unexpected.");
      return ret_fetch_dst;
    }

    // `gathered_fetch_dst != nullptr`.
    if (ret_fetch_dst != nullptr) {
      to_worker_shard_->temp_frame_buffer_.TryDeallocate(ret_fetch_dst);
    }
    ret_fetch_dst = gathered_fetch_dst;

    Callback cur_cb{};
    while (true) {
      bool check{pending_fetch_cb_.try_dequeue(cur_cb)};
      BUSTUB_ASSERT(check, "Unexpected.");
      if (cur_cb.callback_opt_.has_value()) {
        fetch_cb.emplace_back(std::move(cur_cb.callback_opt_.value()));
      }
      if (cur_cb.validation_ptr_ == ret_fetch_dst) {
        break;
      }
    }

    old_state = state_.load(std::memory_order_relaxed);
    if ((old_state & State::PENDING_FETCH) == 0) {
      break;
    }
    /**
     * `state_` 被再次置位 `PENDING_FETCH`.
     */
  }
  BUSTUB_ASSERT(ret_fetch_dst != nullptr || fetch_cb.empty(), "Unexpected.");
  return ret_fetch_dst;
}

auto DiskScheduler::PageTaskBlock::TrySetDequeued() -> bool {
  auto old_state{state_.load(std::memory_order_relaxed)};

  BUSTUB_ASSERT((old_state & State::IS_INQUEUE), "Unexpected.");

  if (old_state != State::IS_INQUEUE) {
    // 存在 `PENDING_*`.
    return false;
  }

  std::uint8_t expected_state{State::IS_INQUEUE};
  return state_.compare_exchange_strong(expected_state, State::EMPTY, std::memory_order_relaxed,
                                        std::memory_order_relaxed);
}

// ========================================
//
// DiskScheduler::PageTaskTable
//
// ========================================
DiskScheduler::PageTaskTable::PageTaskTable() : shards_(simple_cu::utils::concurrency * SHARD_FACTOR) {}

auto DiskScheduler::PageTaskTable::VisitShard(page_id_t page_id) -> PageTaskTableShard * {
  return &shards_[FastHash{}(page_id, 2) % shards_.size()];
}

// ========================================
//
// DiskScheduler::WorkerTable
//
// ========================================
DiskScheduler::WorkerTable::WorkerTable() : shards_(simple_cu::utils::concurrency * SHARD_FACTOR) {}

auto DiskScheduler::WorkerTable::VisitShard(page_id_t page_id) -> WorkerShard * {
  return &shards_[FastHash{}(page_id, 1) % shards_.size()];
}

// ========================================
//
// DiskScheduler
//
// ========================================
DiskScheduler::DiskScheduler(DiskManager *disk_manager)
    : disk_manager_(disk_manager),
      worker_(simple_cu::utils::concurrency * SHARD_FACTOR),
      instance_id_{next_instance_id.fetch_add(1, std::memory_order_relaxed)} {
  fmt::println("DiskScheduler: SHARD_CNT {}.", simple_cu::utils::concurrency);
  for (size_t i = 0; i < worker_.size(); i++) {
    worker_[i] = std::thread{[this, i]() { this->StartWorkerThread(i); }};
  }
}

DiskScheduler::~DiskScheduler() {
  stop_.store(true, std::memory_order_release);
  /**
   * 如果未来支持单 Shard 多 worker, 此处需要投递多个 `nullptr`.
   */
  for (auto &shard : worker_table_.shards_) {
    shard.pending_queue_.Push(nullptr);
  }
  for (auto &worker : worker_) {
    worker.join();
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Schedules a request for the DiskManager to execute.
 *
 * @param r The request to be scheduled.
 *
 * 返回 `false` 则必须同步等待.
 */
auto DiskScheduler::Schedule(DiskRequest r) -> bool {
  BUSTUB_ASSERT(r.page_id_ != INVALID_PAGE_ID, "Unexpected.");
  auto *to_shard{worker_table_.VisitShard(r.page_id_)};

  auto to_task_table{global_ptb_table_.VisitShard(r.page_id_)};
  PageTaskBlock *to_ptb{};

  /**
   * Thread local cached map.
   */
  auto to_cached_ptb_table{&tls_map[instance_id_].cached_ptb_table_};
  if (auto iter{to_cached_ptb_table->find(r.page_id_)}; iter == to_cached_ptb_table->end()) {
    {
      std::lock_guard lk{to_task_table->m_};
      to_ptb = to_task_table->table_.try_emplace(r.page_id_, std::make_unique<PageTaskBlock>(r.page_id_, to_shard))
                   .first->second.get();
    }
    to_cached_ptb_table->emplace(r.page_id_, to_ptb);
  } else {
    to_ptb = iter->second;
  }

  BUSTUB_ASSERT(to_ptb != nullptr, "Unexpected.");

  if (r.is_flush_) {  // Flush
    if (r.async_hint_) {
      char *temp_frame{r.data_};

      if (r.data_ == nullptr) {
        temp_frame = to_shard->temp_frame_buffer_.Allocate();
        to_ptb->SubmitFlushTask(temp_frame, true, std::move(r.callback_));
        return true;
      }

      if (!to_shard->temp_frame_buffer_.TryAllocate(temp_frame)) {
        to_ptb->SubmitFlushTask(r.data_, false, std::move(r.callback_));
        return false;
      }

      std::memcpy(temp_frame, r.data_, BUSTUB_PAGE_SIZE);
      to_ptb->SubmitFlushTask(temp_frame, true, std::move(r.callback_));
    } else {
      BUSTUB_ASSERT(r.data_ != nullptr, "Unexpected.");
      to_ptb->SubmitFlushTask(r.data_, false, std::move(r.callback_));
    }
  } else {  // Fetch
    BUSTUB_ASSERT(r.async_hint_ == false, "Unexpected.");
    to_ptb->SubmitFetchTask(r.data_, false, std::move(r.callback_));
  }
  return r.async_hint_;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 */
void DiskScheduler::StartWorkerThread(size_t shard_id) {
  // auto to_worker_shard{worker_table_.shards_[shard_id].get()};
  auto to_worker_shard{&worker_table_.shards_[shard_id]};
  std::vector<DiskSchedulerPromise> fetch_cb{};
  fetch_cb.reserve(8);
  std::vector<DiskSchedulerPromise> flush_cb{};
  flush_cb.reserve(8);

  while (true) {
    PageTaskBlock *to_ptb{to_worker_shard->pending_queue_.Pop()};
    if (to_ptb == nullptr) {
      while (!stop_.load(std::memory_order_acquire)) {
        ;
      }
      break;
    }
    page_id_t const processing_page_id{to_ptb->ptb_page_id_};

    while (true) {
      auto flush_src{to_ptb->GatherFlushTask(flush_cb)};
      auto fetch_dst{to_ptb->GatherFetchTask(fetch_cb)};

      if (fetch_dst != nullptr && flush_src != nullptr) {
        /**
         * Bypass 路径.
         */
        std::memcpy(fetch_dst, flush_src, BUSTUB_PAGE_SIZE);
        for (auto &cb : fetch_cb) {
          cb.set_value(true);
        }
        disk_manager_->WritePage(processing_page_id, flush_src);
        for (auto &cb : flush_cb) {
          cb.set_value(true);
        }
        to_worker_shard->temp_frame_buffer_.TryDeallocate(fetch_dst);
        to_worker_shard->temp_frame_buffer_.TryDeallocate(flush_src);
      } else {
        if (flush_src != nullptr) {
          disk_manager_->WritePage(processing_page_id, flush_src);
          for (auto &cb : flush_cb) {
            cb.set_value(true);
          }
          to_worker_shard->temp_frame_buffer_.TryDeallocate(flush_src);
        }
        if (fetch_dst != nullptr) {
          disk_manager_->ReadPage(processing_page_id, fetch_dst);
          for (auto &cb : fetch_cb) {
            cb.set_value(true);
          }
          to_worker_shard->temp_frame_buffer_.TryDeallocate(fetch_dst);
        }
      }

      flush_cb.clear();
      fetch_cb.clear();

      if (!to_ptb->TrySetDequeued()) {
        continue;
      }
      break;
    }
  }
}
}  // namespace bustub
