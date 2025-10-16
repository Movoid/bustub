
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
ConcurrentTempFrameBuffer::ConcurrentTempFrameBuffer(size_t temp_frame_cnt) {
  for (size_t i = 0; i < temp_frame_cnt; i++) {
    available_frames_.Push(new (std::align_val_t{simple_cu::utils::CACHELINE_SIZE}) char[BUSTUB_PAGE_SIZE]);
  }
}

ConcurrentTempFrameBuffer::~ConcurrentTempFrameBuffer() {
  std::queue<char *> tmp{};
  available_frames_.PopAll(tmp);
  while (!tmp.empty()) {
    auto ptr{tmp.front()};
    ::operator delete[](ptr, std::align_val_t{simple_cu::utils::CACHELINE_SIZE});
    tmp.pop();
  }
}

auto ConcurrentTempFrameBuffer::Allocate() -> char * { return available_frames_.Pop(); }

auto ConcurrentTempFrameBuffer::TryAllocate(char *&res) -> bool { return available_frames_.TryPop(res); }

void ConcurrentTempFrameBuffer::Deallocate(char *ptr) { available_frames_.Push(ptr); }

// ========================================
//
// DiskScheduler::PageTaskBlock
//
// ========================================
DiskScheduler::PageTaskBlock::PageTaskBlock(page_id_t page_id, WorkerTable::WorkerShard *to_worker_shard)
    : ptb_page_id_{page_id}, to_worker_shard_{to_worker_shard} {}

void DiskScheduler::PageTaskBlock::SubmitFlushSrc(char *new_flush_src, bool is_deallocatable,
                                                  DiskSchedulerPromise &&callback) {
  char *old_temp_frame{};
  bool need_deallcate{};
  {
    std::lock_guard lk{m_};
    BUSTUB_ASSERT(flush_src_ == nullptr || flush_src_is_deallocable_, "Unexpected.");
    old_temp_frame = std::exchange(flush_src_, new_flush_src);
    need_deallcate = std::exchange(flush_src_is_deallocable_, is_deallocatable);
    pending_flush_cb_.emplace_back(std::move(callback));
    if (!is_inqueue_) {
      to_worker_shard_->pending_queue_.Push(this);
      is_inqueue_ = true;
    }
  }
  if (old_temp_frame != nullptr && need_deallcate) {
    to_worker_shard_->temp_frame_buffer_.Deallocate(old_temp_frame);
  }
}

void DiskScheduler::PageTaskBlock::SubmitFetchDst(char *new_fetch_dst, bool is_deallocatable,
                                                  DiskSchedulerPromise &&callback) {
  char *old_temp_frame{};
  bool need_deallocate{};
  {
    std::lock_guard lk{m_};
    BUSTUB_ASSERT(fetch_dst_ == nullptr || fetch_dst_is_deallocable_, "Unexpected.");
    old_temp_frame = std::exchange(fetch_dst_, new_fetch_dst);
    need_deallocate = std::exchange(fetch_dst_is_deallocable_, is_deallocatable);
    pending_fetch_cb_.emplace_back(std::move(callback));
    if (!is_inqueue_) {
      to_worker_shard_->pending_queue_.Push(this);
      is_inqueue_ = true;
    }
  }
  if (old_temp_frame != nullptr && need_deallocate) {
    to_worker_shard_->temp_frame_buffer_.Deallocate(old_temp_frame);
  }
}

auto DiskScheduler::PageTaskBlock::GatherAllTasks(std::vector<DiskSchedulerPromise> &flush_cb,
                                                  std::vector<DiskSchedulerPromise> &fetch_cb)
    -> std::tuple<char *, bool, char *, bool> {
  flush_cb.reserve(pending_flush_cb_.size());
  fetch_cb.reserve(pending_fetch_cb_.size());
  {
    std::lock_guard lk{m_};
    flush_cb.insert(flush_cb.end(), std::make_move_iterator(pending_flush_cb_.begin()),
                    std::make_move_iterator(pending_flush_cb_.end()));
    fetch_cb.insert(fetch_cb.end(), std::make_move_iterator(pending_fetch_cb_.begin()),
                    std::make_move_iterator(pending_fetch_cb_.end()));
    pending_flush_cb_.clear();
    pending_fetch_cb_.clear();
    is_inqueue_ = false;
    return {std::exchange(flush_src_, nullptr), std::exchange(flush_src_is_deallocable_, false),
            std::exchange(fetch_dst_, nullptr), std::exchange(fetch_dst_is_deallocable_, false)};
  }
}

// ========================================
//
// DiskScheduler::PageTaskTable
//
// ========================================
DiskScheduler::PageTaskTable::PageTaskTable() : shards_(PAGETASKTABLESHARD_CNT) {
  for (auto &shard : shards_) {
    shard = std::make_unique<PageTaskTableShard>();
  }
}

auto DiskScheduler::PageTaskTable::VisitShard(page_id_t page_id) -> PageTaskTableShard * {
  return shards_[FastHash{}(page_id, 2) % shards_.size()].get();
}

// ========================================
//
// DiskScheduler::WorkerTable
//
// ========================================
DiskScheduler::WorkerTable::WorkerTable() : shards_(WORKERSHARD_CNT) {
  for (auto &shard : shards_) {
    shard = std::make_unique<WorkerShard>();
  }
}

auto DiskScheduler::WorkerTable::VisitShard(page_id_t page_id) -> WorkerShard * {
  return shards_[FastHash{}(page_id, 1) % shards_.size()].get();
}

// ========================================
//
// DiskScheduler
//
// ========================================
DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  fmt::println("DiskScheduler: SHARD_CNT {}.", simple_cu::utils::concurrency);
  for (size_t i = 0; i < worker_table_.shards_.size(); i++) {
    auto to_worker_shard{worker_table_.shards_[i].get()};
    for (auto &worker : to_worker_shard->worker_) {
      worker = std::thread{[this, i] { this->StartWorkerThread(i); }};
    }
  }
}

DiskScheduler::~DiskScheduler() {
  stop_.store(true, std::memory_order_release);
  /**
   * 如果未来支持单 Shard 多 worker, 此处需要投递多个 `nullptr`.
   */
  for (auto &shard : worker_table_.shards_) {
    for (size_t i = 0; i < shard->worker_.size(); i++) {
      shard->pending_queue_.Push(nullptr);
    }
  }
  for (auto &shard : worker_table_.shards_) {
    for (auto &worker : shard->worker_) {
      worker.join();
    }
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

  auto to_task_table{to_shard->ptb_table_.VisitShard(r.page_id_)};
  PageTaskBlock *to_ptb{};

  /**
   * Thread local cached map.
   */
  static thread_local std::unordered_map<page_id_t, PageTaskBlock *> cached_ptb_table;
  if (auto iter{cached_ptb_table.find(r.page_id_)}; iter == cached_ptb_table.end()) {
    {
      std::lock_guard lk{to_task_table->m_};
      to_ptb = &to_task_table->table_.try_emplace(r.page_id_, r.page_id_, to_shard).first->second;
    }
    cached_ptb_table.emplace(r.page_id_, to_ptb);
  } else {
    to_ptb = iter->second;
  }

  BUSTUB_ASSERT(to_ptb != nullptr, "Unexpected.");

  if (r.is_flush_) {  // Flush
    if (r.async_hint_) {
      char *temp_frame{r.data_};

      if (r.data_ == nullptr) {
        temp_frame = to_shard->temp_frame_buffer_.Allocate();
        to_ptb->SubmitFlushSrc(temp_frame, true, std::move(r.callback_));
        return true;
      }

      if (!to_shard->temp_frame_buffer_.TryAllocate(temp_frame)) {
        to_ptb->SubmitFlushSrc(r.data_, false, std::move(r.callback_));
        return false;
      }

      std::memcpy(temp_frame, r.data_, BUSTUB_PAGE_SIZE);
      to_ptb->SubmitFlushSrc(temp_frame, true, std::move(r.callback_));
    } else {
      BUSTUB_ASSERT(r.data_ != nullptr, "Unexpected.");
      to_ptb->SubmitFlushSrc(r.data_, false, std::move(r.callback_));
    }
  } else {  // Fetch
    BUSTUB_ASSERT(r.async_hint_ == false, "Unexpected.");
    to_ptb->SubmitFetchDst(r.data_, false, std::move(r.callback_));
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
  auto to_worker_shard{worker_table_.shards_[shard_id].get()};
  std::vector<DiskSchedulerPromise> fetch_cb{};
  std::vector<DiskSchedulerPromise> flush_cb{};

  while (true) {
    PageTaskBlock *to_ptb{to_worker_shard->pending_queue_.Pop()};
    if (to_ptb == nullptr) {
      while (!stop_.load(std::memory_order_acquire)) {
        ;
      }
      break;
    }
    page_id_t processing_page_id{to_ptb->ptb_page_id_};

    while (true) {
      auto [flush_src, flush_need_deallocate, fetch_dst,
            fetch_need_deallocate]{to_ptb->GatherAllTasks(flush_cb, fetch_cb)};
      if (flush_src == nullptr && fetch_dst == nullptr) {
        break;
      }

      if (fetch_dst != nullptr && flush_src != nullptr) {
        /**
         * Bypass 路径.
         */
        std::memcpy(fetch_dst, flush_src, BUSTUB_PAGE_SIZE);
        for (auto &cb : fetch_cb) {
          cb.set_value(true);
        }
        fetch_cb.clear();
        disk_manager_->WritePage(processing_page_id, flush_src);
        for (auto &cb : flush_cb) {
          cb.set_value(true);
        }
        flush_cb.clear();
        if (fetch_need_deallocate) {
          to_worker_shard->temp_frame_buffer_.Deallocate(fetch_dst);
        }
        if (flush_need_deallocate) {
          to_worker_shard->temp_frame_buffer_.Deallocate(flush_src);
        }
        continue;
      }

      if (flush_src != nullptr) {
        disk_manager_->WritePage(processing_page_id, flush_src);
        for (auto &cb : flush_cb) {
          cb.set_value(true);
        }
        flush_cb.clear();
        if (flush_need_deallocate) {
          to_worker_shard->temp_frame_buffer_.Deallocate(flush_src);
        }
      }

      if (fetch_dst != nullptr) {
        disk_manager_->ReadPage(processing_page_id, fetch_dst);
        for (auto &cb : fetch_cb) {
          cb.set_value(true);
        }
        fetch_cb.clear();
        if (fetch_need_deallocate) {
          to_worker_shard->temp_frame_buffer_.Deallocate(fetch_dst);
        }
      }
    }
  }
}
}  // namespace bustub
