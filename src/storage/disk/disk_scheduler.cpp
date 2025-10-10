
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
#include <chrono>
#include <cstring>
#include "common/exception.h"
#include "common/macros.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // for (auto &worker : workers_) {
  //   worker = std::thread{[&] { StartWorkerThread(); }};
  // }
  std::cout << "Concurrency: " << std::thread::hardware_concurrency() << std::endl;
  for (size_t i = 0; i < 40; i++) {
    workers_[i] = std::thread{[&] { StartWorkerThread(); }};
  }
}

DiskScheduler::~DiskScheduler() {
  {
    std::lock_guard<std::mutex> awaited_lk{awaited_m_};
    stop_ = true;
    cond_.notify_all();
  }
  for (size_t i = 0; i < 40; i++) {
    workers_[i].join();
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Schedules a request for the DiskManager to execute.
 *
 * @param r The request to be scheduled.
 */
void DiskScheduler::Schedule(DiskRequest r) {
  TemporaryPageBuffer::Page *new_buf{};
  if (r.is_write_) {
    new_buf = fb_.Pop();
  }

  {
    std::lock_guard<std::mutex> queues_lk{queues_m_};
    page_id_t page_id{r.page_id_};

    /** Flush requests. */
    if (r.is_write_) {
      auto old_buf{temp_flush_buffer_[r.page_id_]};
      if (old_buf == nullptr) {
        if (r.data_ == nullptr) {
          std::memset(new_buf, 0, BUSTUB_PAGE_SIZE);
        } else {
          std::memcpy(new_buf, r.data_, BUSTUB_PAGE_SIZE);
        }
        temp_flush_buffer_[r.page_id_] = new_buf;
      } else {
        BUSTUB_ASSERT(r.data_ != nullptr, "Unexpected.");
        std::memcpy(old_buf, r.data_, BUSTUB_PAGE_SIZE);
        fb_.Push(new_buf);
      }
      r.data_ = nullptr;
    }

    {
      std::lock_guard<std::mutex> awaited_lk{awaited_m_};
      /**
       * `queues_[page_id].second == true` 标记有线程正在处理此 page queue,
       * 不要向 `awaited_` 添加 token.
       */
      if (queues_[page_id].first.empty() && !queues_[page_id].second) {
        awaited_.emplace(page_id);
        cond_.notify_one();
      }
    }
    queues_[page_id].first.emplace(std::move(r));
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 */
void DiskScheduler::StartWorkerThread() {
  DiskRequest diskreq;
  page_id_t cur_page_id{};
  while (true) {
    {
      std::unique_lock<std::mutex> awaited_lk{awaited_m_};
      cond_.wait(awaited_lk, [&] { return stop_ || !awaited_.empty(); });

      if (stop_ && awaited_.empty()) {
        break;
      }

      BUSTUB_ASSERT(awaited_.begin() != awaited_.end(), "Scheduler");

      if (next_iter_ == awaited_.end()) {
        if (to_high_) {
          to_high_ = false;
          next_iter_ = std::prev(awaited_.end());
        } else {
          to_high_ = true;
          next_iter_ = awaited_.begin();
        }
      }

      std::set<page_id_t>::iterator cur_iter{next_iter_};
      cur_page_id = *cur_iter;

      if (to_high_) {
        next_iter_ = std::next(cur_iter);
      } else {
        next_iter_ = cur_iter == awaited_.begin() ? awaited_.end() : std::prev(cur_iter);
      }

      awaited_.erase(cur_iter);
    }
    while (true) {
      TemporaryPageBuffer::Page *flush_buf{};
      {
        std::lock_guard<std::mutex> queues_lk{queues_m_};
        if (queues_[cur_page_id].first.empty()) {
          // 标记线程退出处理此 page queue.
          queues_[cur_page_id].second = false;  // This position is important.
          break;
        }
        diskreq = std::move(queues_[cur_page_id].first.front());
        queues_[cur_page_id].first.pop();
        queues_[cur_page_id].second = true;
        /** Hook flush requests. */
        if (diskreq.is_write_) {
          flush_buf = temp_flush_buffer_[cur_page_id];
          temp_flush_buffer_[cur_page_id] = nullptr;
        }
      }
      if (diskreq.dependency_.has_value()) {
        diskreq.dependency_->get();
      }
      /**
       * 注意此处, 执行 diskreq 时没有 queues_m_ 锁,
       * 可能有线程判断 queue 为空, 并提交到 `awaited_`.
       * 此 Race 可能存在于过去的 commits 中没有暴露.
       */
      if (diskreq.is_write_) {  // Flush
        if (flush_buf != nullptr) {
          disk_manager_->WritePage(diskreq.page_id_, flush_buf->data_);
          fb_.Push(flush_buf);
        }
      } else {  // Fetch
        disk_manager_->ReadPage(diskreq.page_id_, diskreq.data_);
      }
      diskreq.callback_.set_value(true);
    }
  }
}
}  // namespace bustub
