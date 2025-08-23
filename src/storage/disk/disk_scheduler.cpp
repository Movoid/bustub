
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

#include "storage/disk/disk_scheduler.h"
#include <chrono>
#include "common/exception.h"
#include "common/macros.h"
#include "storage/disk/disk_manager.h"

// #define CUSTOM_LOG
// #include "common/custom_debug.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // TODO(P1): remove this line after you have implemented the disk scheduler API
  // Spawn the background thread
  // background_thread_.emplace([&] { StartWorkerThread(); });
  for (auto &worker : workers_) {
    worker = std::thread{[&] { StartWorkerThread(); }};
  }
}

DiskScheduler::~DiskScheduler() {
  {
    std::lock_guard<std::mutex> awaited_lk{awaited_m_};
    stop_ = true;
    cond_.notify_all();
  }
  // background_thread_->join();
  for (auto &worker : workers_) {
    worker.join();
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
  // if (r.is_write_) {
  //   DebugLog("Submitting write task, page {} .", r.page_id_);
  // } else {
  //   DebugLog("Submitting read task, page {} .", r.page_id_);
  // }

  {
    std::lock_guard<std::mutex> queues_lk{queues_m_};
    page_id_t page_id{r.page_id_};
    {
      std::lock_guard<std::mutex> awaited_lk{awaited_m_};
      if (queues_[page_id].empty()) {
        awaited_.emplace(page_id);
        cond_.notify_one();
      }
    }
    queues_[page_id].emplace(std::move(r));
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 *
 * 注意, 只允许单线程, 否则需要处理 `cur_iter` 因 Race 导致的失效.
 */
void DiskScheduler::StartWorkerThread() {
  DiskRequest diskreq;
  page_id_t cur_page_id{};
  while (true) {
    {
      std::unique_lock<std::mutex> awaited_lk{awaited_m_};
      // std::set<page_id_t>::iterator cur_iter{};
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

      // cur_iter = next_iter_;
      // cur_page_id = *cur_iter;

      cur_page_id = *next_iter_;
      if (to_high_) {
        next_iter_ = std::next(next_iter_);
      } else {
        next_iter_ = next_iter_ == awaited_.begin() ? awaited_.end() : std::prev(next_iter_);
      }
    }
    // Get a DiskRequest.
    int cnt{};
    while (true) {
      {
        std::lock_guard<std::mutex> queues_lk{queues_m_};
        {
          std::lock_guard<std::mutex> awaited_lk{awaited_m_};
          auto iter{awaited_.find(cur_page_id)};
          if (iter == awaited_.end()) {
            break;
          }
          if (queues_[cur_page_id].empty()) {
            // awaited_.erase(cur_iter);  // Iterator is always valid.
            if (iter == next_iter_) {
              next_iter_ = std::next(next_iter_);
            }
            awaited_.erase(iter);
            break;
          }
        }
        diskreq = std::move(queues_[cur_page_id].front());
        cnt++;
        queues_[cur_page_id].pop();
      }
      if (diskreq.is_write_) {
        disk_manager_->WritePage(diskreq.page_id_, diskreq.data_);
        // DebugLog("Finished write page {}.", diskreq.page_id_);
      } else {
        disk_manager_->ReadPage(diskreq.page_id_, diskreq.data_);
        // DebugLog("Finished read page {}.", diskreq.page_id_);
      }
      diskreq.callback_.set_value(true);

      if (cnt > 10) {
        break;
      }
    }
  }
}
}  // namespace bustub
