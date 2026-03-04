//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

// #define CUSTOM_LOG
// #include "common/custom_debug.h"

#include "buffer/buffer_pool_manager.h"
#include "common/logger.h"

namespace bustub {

// ========================================
//
// FrameHeader
//
// ========================================

/**
 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
 *
 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
 *
 * @param frame_id The frame ID / index of the frame we are creating a header for.
 */
FrameHeader::FrameHeader(frame_id_t frame_id) : data_(BUSTUB_PAGE_SIZE, 0), frame_id_(frame_id) { Reset(); }

/**
 * @brief Get a raw const pointer to the frame's data.
 *
 * @return const char* A pointer to immutable data that the frame stores.
 */
auto FrameHeader::GetData() const -> const char * { return data_.data(); }

/**
 * @brief Get a raw mutable pointer to the frame's data.
 *
 * @return char* A pointer to mutable data that the frame stores.
 */
auto FrameHeader::GetDataMut() -> char * { return data_.data(); }

/**
 * @brief Resets a `FrameHeader`'s member fields.
 * `pin_count_` 作为关键数据应该手动处理.
 */
void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  is_dirty_ = false;
  page_id_ = INVALID_PAGE_ID;
}

// ========================================
//
// BufferPoolManager::PageTable
//
// ========================================

BufferPoolManager::PageTable::PageTable() : shards_(simple_cu::utils::concurrency * SHARD_FACTOR) {}

auto BufferPoolManager::PageTable::VisitShard(page_id_t page_id) -> PageTableShard * {
  return &shards_[FastHash{}(page_id, 1) % shards_.size()];
}

// ========================================
//
// BufferPoolManager
//
// ========================================

/**
 * @brief Creates a new `BufferPoolManager` instance and initializes all fields.
 *
 * See the documentation for `BufferPoolManager` in "buffer/buffer_pool_manager.h" for more information.
 *
 * ### Implementation
 *
 * We have implemented the constructor for you in a way that makes sense with our reference solution. You are free to
 * change anything you would like here if it doesn't fit with you implementation.
 *
 * Be warned, though! If you stray too far away from our guidance, it will be much harder for us to help you. Our
 * recommendation would be to first implement the buffer pool manager using the stepping stones we have provided.
 *
 * Once you have a fully working solution (all Gradescope test cases pass), then you can try more interesting things!
 *
 * @param num_frames The size of the buffer pool.
 * @param disk_manager The disk manager.
 * @param k_dist The backward k-distance for the LRU-K replacer.
 * @param log_manager The log manager. Please ignore this for P1.
 */
BufferPoolManager::BufferPoolManager(size_t num_frames, DiskManager *disk_manager, size_t k_dist,
                                     LogManager *log_manager)
    : num_frames_(num_frames),
      next_page_id_(0),
      replacer_(std::make_shared<ShardingLRUKReplacer>(num_frames, k_dist)),
      disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)),
      log_manager_(log_manager) {
  // Initialize the monotonically increasing counter at 0.
  next_page_id_.store(0);

  // Allocate all of the in-memory frames up front.
  frames_.reserve(num_frames_);

  for (frame_id_t frame_id = 0; frame_id < static_cast<frame_id_t>(num_frames); frame_id++) {
    frames_.emplace_back(std::make_unique<FrameHeader>(frame_id));
  }

  for (auto &ref_shard : page_table_.shards_) {
    ref_shard.map_.reserve((num_frames + page_table_.shards_.size() - 1) / page_table_.shards_.size());
  }

  /**
   * Patch for fixing early delete problem in bustub testcases.
   */
  disk_manager->RegisterDiskSchedulerForDebugOnly(disk_scheduler_);
}

/**
 * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
 *
 * Support callback.
 */
BufferPoolManager::~BufferPoolManager() = default;

/**
 * @brief Returns the number of frames that this buffer pool manages.
 */
auto BufferPoolManager::Size() const -> size_t { return num_frames_; }

/**
 * @brief Helper. 将一个 disk page 数据读到 memory frame.
 *
 * 读 disk.
 */
void BufferPoolManager::FetchPageUnsafe(page_id_t page_id, char *dst) {  // NOLINT
  auto promise{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{promise.get_future()};
  DiskRequest request{false, dst, page_id, std::move(promise), false};
  [[maybe_unused]] bool is_async{disk_scheduler_->Schedule(std::move(request))};
  fut.get();
}

/**
 * @brief Helper. 将一个 memory frame 数据写到 disk page.
 *
 * 写 disk.
 */
void BufferPoolManager::FlushPageUnsafe(page_id_t page_id, char *src) {  // NOLINT
  auto promise{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{promise.get_future()};
  DiskRequest request{true, src, page_id, std::move(promise), false};
  [[maybe_unused]] bool is_async{disk_scheduler_->Schedule(std::move(request))};
  fut.get();
}

void BufferPoolManager::FetchPageAsyncUnsafe(page_id_t page_id, char *dst) {  // NOLINT
  auto promise{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{promise.get_future()};
  DiskRequest request{false, dst, page_id, std::move(promise), true};
  bool is_async{disk_scheduler_->Schedule(std::move(request))};
  if (!is_async) {
    fut.get();
  }
}

/**
 * `Schedule` 会对 frame data 做一份拷贝, 以实现 async.
 * 不会修改原本 frame data.
 */
void BufferPoolManager::FlushPageAsyncUnsafe(page_id_t page_id, char *src) {  // NOLINT
  auto promise{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{promise.get_future()};
  DiskRequest request{true, src, page_id, std::move(promise), true};
  bool is_async{disk_scheduler_->Schedule(std::move(request))};
  if (!is_async) {
    fut.get();
  }
}

/**
 * @brief Allocates a new page on disk.
 *
 * ### Implementation
 *
 * You will maintain a thread-safe, monotonically increasing counter in the form of a `std::atomic<page_id_t>`.
 * See the documentation on [atomics](https://en.cppreference.com/w/cpp/atomic/atomic) for more information.
 *
 * TODO(P1): Add implementation.
 *
 * @return The page ID of the newly allocated page.
 */
auto BufferPoolManager::NewPage() -> page_id_t {
  /**
   * 必须调用 `DiskManager` 的 `WritePage`.
   * 否则 `ReadPage` 报错 page not in range.
   */
  page_id_t new_page_id{next_page_id_.fetch_add(1, std::memory_order_relaxed)};

  char test_buf[BUSTUB_PAGE_SIZE]{};
  FlushPageAsyncUnsafe(new_page_id, test_buf);

  return new_page_id;
}

/**
 * @brief Removes a page from the database, both on disk and in memory.
 *
 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
 *
 * ### Implementation
 *
 * Think about all of the places a page or a page's metadata could be, and use that to guide you on implementing this
 * function. You will probably want to implement this function _after_ you have implemented `CheckedReadPage` and
 * `CheckedWritePage`.
 *
 * You should call `DeallocatePage` in the disk scheduler to make the space available for new pages.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to delete.
 * @return `false` if the page exists but could not be deleted, `true` if the page didn't exist or deletion succeeded.
 *
 * 删除 page, 需要删除 disk 上的数据.
 */
auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  auto to_table_shard{page_table_.VisitShard(page_id)};
  {
    std::lock_guard lk{to_table_shard->m_};
    auto to_map{&to_table_shard->map_};
    auto iter{to_map->find(page_id)};
    if (iter == to_map->end()) {  // Not in page table.
      disk_scheduler_->DeallocatePage(page_id);
      return true;
    }
    frame_id_t frame_id{iter->second};  // In page table.
    if (!replacer_->TryResetRecord(frame_id)) {
      return false;
    }
    FrameHeader *to_frame{frames_[frame_id].get()};
    BUSTUB_ASSERT(to_map->count(page_id) == 1, "Unexpected.");
    to_map->erase(page_id);
    to_frame->Reset();
    disk_scheduler_->DeallocatePage(page_id);
  }
  return true;
}
/**
 * @brief Acquires an optional write-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can only be 1 `WritePageGuard` reading/writing a page at a time. This allows data access to be both immutable
 * and mutable, meaning the thread that owns the `WritePageGuard` is allowed to manipulate the page's data however they
 * want. If a user wants to have multiple threads reading the page at the same time, they must acquire a `ReadPageGuard`
 * with `CheckedReadPage` instead.
 *
 * ### Implementation
 *
 * There are 3 main cases that you will have to implement. The first two are relatively simple: one is when there is
 * plenty of available memory, and the other is when we don't actually need to perform any additional I/O. Think about
 * what exactly these two cases entail.
 *
 * The third case is the trickiest, and it is when we do not have any _easily_ available memory at our disposal. The
 * buffer pool is tasked with finding memory that it can use to bring in a page of memory, using the replacement
 * algorithm you implemented previously to find candidate frames for eviction.
 *
 * Once the buffer pool has identified a frame for eviction, several I/O operations may be necessary to bring in the
 * page of data we want into the frame.
 *
 * There is likely going to be a lot of shared code with `CheckedReadPage`, so you may find creating helper functions
 * useful.
 *
 * These two functions are the crux of this project, so we won't give you more hints than this. Good luck!
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to write to.
 * @param access_type The type of page access.
 * @return std::optional<WritePageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`, otherwise returns a `WritePageGuard` ensuring exclusive and mutable access to a page's data.
 */
auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  if (page_id == INVALID_PAGE_ID) {
    return std::nullopt;
  }

  while (true) {
    frame_id_t frame_id{INVALID_FRAME_ID};

    auto to_table_shard{page_table_.VisitShard(page_id)};
    {
      std::lock_guard lk{to_table_shard->m_};
      auto to_map{&to_table_shard->map_};
      auto iter{to_map->find(page_id)};
      if (iter != to_map->end()) {
        frame_id = iter->second;
        replacer_->PinAndRecordAccess(frame_id);
      }
    }

    if (frame_id != INVALID_FRAME_ID) {
      return WritePageGuard{page_id, frames_[frame_id], replacer_};
    }

    /**
     * `page_table_` 没有找到.
     * 可能需要加载.
     * 注意可能在此时已经被其他线程加载到某个 frame.
     */

    auto evicted{replacer_->SelectEvictAndPin()};
    if (!evicted.has_value()) {
      LOG_WARN("BufferPoolManager failed eviction.");
      return std::nullopt;
    }

    frame_id_t evicted_frame_id{evicted.value()};

    {
      std::lock_guard frame_lk{frames_[evicted_frame_id]->rwlatch_};
      FrameHeader *to_evicted_frame{frames_[evicted_frame_id].get()};

      /**
       * 此处可能涉及两个 `PageTableShard` 的修改, 较为棘手.
       * 以及注意 `to_frame` 可能被其他线程更多 Pinned.
       */

      /**
       * Unlink 被 evicted 的 page.
       */
      BUSTUB_ASSERT(to_evicted_frame->page_id_ != INVALID_PAGE_ID || !to_evicted_frame->is_dirty_, "Unexpected.");
      if (to_evicted_frame->page_id_ != INVALID_PAGE_ID) {
        auto to_evicted_table_shard{page_table_.VisitShard(to_evicted_frame->page_id_)};

        if (to_evicted_frame->is_dirty_) {
          FlushPageAsyncUnsafe(to_evicted_frame->page_id_, to_evicted_frame->GetDataMut());
          to_evicted_frame->is_dirty_ = false;
        }

        {
          std::lock_guard evicted_map_lk{to_evicted_table_shard->m_};

          /**
           * 在此处 `TryCommitEvict`.
           * 必须持有 `evicted_map_lk`, 否则在 `TryCommitEvict` 成功后,
           * 其他线程仍能通过 map 找到 carry 某 page 的这个 frame 并 `Pin`.
           *
           * 若先删除 map 映射, 再解锁再 `TryCommitEvict`,
           * 则可能由于换出的 Flush 还未开始, 就有线程成功 fetch 此 page 到其他 frame,
           * 导致 Flush 了旧数据.
           */
          if (!replacer_->TryCommitEvict(evicted_frame_id)) {
            continue;
          }

          auto to_evicted_map{&to_evicted_table_shard->map_};
          BUSTUB_ASSERT(to_evicted_map->count(to_evicted_frame->page_id_) == 1, "Unexpected.");
          to_evicted_map->erase(to_evicted_frame->page_id_);
        }
        to_evicted_frame->page_id_ = INVALID_PAGE_ID;
      }

      /**
       * 检查要被 load 的 page.
       */
      auto to_load_table_shard{page_table_.VisitShard(page_id)};
      {
        std::lock_guard lk{to_load_table_shard->m_};
        auto to_load_map{&to_load_table_shard->map_};
        auto iter{to_load_map->find(page_id)};
        /**
         * 若已经被其他线程抢先 load,
         * `Unpin` 被 Evict 的 frame, 并 Restart.
         */
        if (iter != to_load_map->end()) {
          replacer_->Unpin(evicted_frame_id);
          continue;
        }
        /**
         * 没有被 load. 当前线程需要将 page link & load.
         */
        to_load_map->emplace(page_id, evicted_frame_id);
      }

      /**
       * Load page 并更新 frame metainfo.
       */
      FetchPageUnsafe(page_id, to_evicted_frame->GetDataMut());
      replacer_->RecordAccess(evicted_frame_id);
      to_evicted_frame->page_id_ = page_id;
    }

    return WritePageGuard{page_id, frames_[evicted_frame_id], replacer_};
  }
}

/**
 * @brief Acquires an optional read-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can be any number of `ReadPageGuard`s reading the same page of data at a time across different threads.
 * However, all data access must be immutable. If a user wants to mutate the page's data, they must acquire a
 * `WritePageGuard` with `CheckedWritePage` instead.
 *
 * ### Implementation
 *
 * See the implementation details of `CheckedWritePage`.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return std::optional<ReadPageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`, otherwise returns a `ReadPageGuard` ensuring shared and read-only access to a page's data.
 */
auto BufferPoolManager::CheckedReadPage(page_id_t page_id, AccessType access_type) -> std::optional<ReadPageGuard> {
  if (page_id == INVALID_PAGE_ID) {
    return std::nullopt;
  }

  while (true) {
    frame_id_t frame_id{INVALID_FRAME_ID};

    auto to_table_shard{page_table_.VisitShard(page_id)};
    {
      std::lock_guard lk{to_table_shard->m_};
      auto to_map{&to_table_shard->map_};
      auto iter{to_map->find(page_id)};
      if (iter != to_map->end()) {
        frame_id = iter->second;
        replacer_->PinAndRecordAccess(frame_id);
      }
    }

    if (frame_id != INVALID_FRAME_ID) {
      return ReadPageGuard{page_id, frames_[frame_id], replacer_};
    }

    /**
     * `page_table_` 没有找到.
     * 可能需要加载.
     * 注意可能在此时已经被其他线程加载到某个 frame.
     */

    auto evicted{replacer_->SelectEvictAndPin()};
    if (!evicted.has_value()) {
      LOG_WARN("BufferPoolManager failed eviction.");
      return std::nullopt;
    }

    frame_id_t evicted_frame_id{evicted.value()};

    {
      std::lock_guard frame_lk{frames_[evicted_frame_id]->rwlatch_};
      FrameHeader *to_evicted_frame{frames_[evicted_frame_id].get()};

      /**
       * 此处可能涉及两个 `PageTableShard` 的修改, 较为棘手.
       * 以及注意 `to_frame` 可能被其他线程更多 Pinned.
       */

      /**
       * Unlink 被 evicted 的 page.
       */
      BUSTUB_ASSERT(to_evicted_frame->page_id_ != INVALID_PAGE_ID || !to_evicted_frame->is_dirty_, "Unexpected.");
      if (to_evicted_frame->page_id_ != INVALID_PAGE_ID) {
        auto to_evicted_table_shard{page_table_.VisitShard(to_evicted_frame->page_id_)};

        if (to_evicted_frame->is_dirty_) {
          FlushPageAsyncUnsafe(to_evicted_frame->page_id_, to_evicted_frame->GetDataMut());
          to_evicted_frame->is_dirty_ = false;
        }

        {
          std::lock_guard evicted_map_lk{to_evicted_table_shard->m_};

          /**
           * 在此处 `TryCommitEvict`.
           * 必须持有 `evicted_map_lk`, 否则在 `TryCommitEvict` 成功后,
           * 其他线程仍能通过 map 找到 carry 某 page 的这个 frame 并 `Pin`.
           *
           * 若先删除 map 映射, 再解锁再 `TryCommitEvict`,
           * 则可能由于换出的 Flush 还未开始, 就有线程成功 fetch 此 page 到其他 frame,
           * 导致 Flush 了旧数据.
           */
          if (!replacer_->TryCommitEvict(evicted_frame_id)) {
            continue;
          }

          auto to_evicted_map{&to_evicted_table_shard->map_};
          BUSTUB_ASSERT(to_evicted_map->count(to_evicted_frame->page_id_) == 1, "Unexpected.");
          to_evicted_map->erase(to_evicted_frame->page_id_);
        }
        to_evicted_frame->page_id_ = INVALID_PAGE_ID;
      }

      /**
       * 检查要被 load 的 page.
       */
      auto to_load_table_shard{page_table_.VisitShard(page_id)};
      {
        std::lock_guard lk{to_load_table_shard->m_};
        auto to_load_map{&to_load_table_shard->map_};
        auto iter{to_load_map->find(page_id)};
        /**
         * 若已经被其他线程抢先 load,
         * `Unpin` 被 Evict 的 frame, 并 Restart.
         */
        if (iter != to_load_map->end()) {
          replacer_->Unpin(evicted_frame_id);
          continue;
        }
        /**
         * 没有被 load. 当前线程需要将 page link & load.
         */
        to_load_map->emplace(page_id, evicted_frame_id);
      }

      /**
       * Load page 并更新 frame metainfo.
       */
      FetchPageUnsafe(page_id, to_evicted_frame->GetDataMut());
      replacer_->RecordAccess(evicted_frame_id);
      to_evicted_frame->page_id_ = page_id;
    }

    return ReadPageGuard{page_id, frames_[evicted_frame_id], replacer_};
  }
}

/**
 * @brief A wrapper around `CheckedWritePage` that unwraps the inner value if it exists.
 *
 * If `CheckedWritePage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageWrite` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return WritePageGuard A page guard ensuring exclusive and mutable access to a page's data.
 */
auto BufferPoolManager::WritePage(page_id_t page_id, AccessType access_type) -> WritePageGuard {
  auto guard_opt = CheckedWritePage(page_id, access_type);

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief A wrapper around `CheckedReadPage` that unwraps the inner value if it exists.
 *
 * If `CheckedReadPage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageRead` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return ReadPageGuard A page guard ensuring shared and read-only access to a page's data.
 */
auto BufferPoolManager::ReadPage(page_id_t page_id, AccessType access_type) -> ReadPageGuard {
  auto guard_opt = CheckedReadPage(page_id, access_type);

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief Flushes a page's data out to disk.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage` and
 * `CheckedWritePage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page to be flushed.
 * @return `false` if the page could not be found in the page table, otherwise `true`.
 */
auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  if (page_id == INVALID_PAGE_ID) {
    throw Exception{"Invalid args."};
  }
  frame_id_t frame_id{INVALID_FRAME_ID};
  auto to_table_shard{page_table_.VisitShard(page_id)};
  {
    std::lock_guard lk{to_table_shard->m_};
    auto to_map{&to_table_shard->map_};
    auto iter{to_map->find(page_id)};
    if (iter == to_map->end()) {
      return false;
    }
    frame_id = iter->second;
    replacer_->Pin(frame_id);
  }

  FrameHeader *to_frame{frames_[frame_id].get()};
  if (to_frame->is_dirty_) {
    FlushPageAsyncUnsafe(page_id, to_frame->GetDataMut());
    to_frame->is_dirty_ = false;
  }
  replacer_->Unpin(frame_id);

  return true;
}

/**
 * @brief Flushes all page data that is in memory to disk.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 */
void BufferPoolManager::FlushAllPages() {
  for (auto &to_frame : frames_) {
    {
      std::shared_lock<std::shared_mutex> frame_guard{to_frame->rwlatch_};
      if (to_frame->page_id_ == INVALID_PAGE_ID || !to_frame->is_dirty_) {
        continue;
      }
      FlushPageAsyncUnsafe(to_frame->page_id_, to_frame->GetDataMut());
      to_frame->is_dirty_ = false;
    }
  }
}

/**
 * @brief Retrieves the pin count of a page. If the page does not exist in memory, return `std::nullopt`.
 *
 * This function is thread safe. Callers may invoke this function in a multi-threaded environment where multiple threads
 * access the same page.
 *
 * This function is intended for testing purposes. If this function is implemented incorrectly, it will definitely cause
 * problems with the test suite and autograder.
 *
 * # Implementation
 *
 * We will use this function to test if your buffer pool manager is managing pin counts correctly. Since the
 * `pin_count_` field in `FrameHeader` is an atomic type, you do not need to take the latch on the frame that holds the
 * page we want to look at. Instead, you can simply use an atomic `load` to safely load the value stored. You will still
 * need to take the buffer pool latch, however.
 *
 * Again, if you are unfamiliar with atomic types, see the official C++ docs
 * [here](https://en.cppreference.com/w/cpp/atomic/atomic).
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page we want to get the pin count of.
 * @return std::optional<size_t> The pin count if the page exists, otherwise `std::nullopt`.
 */
auto BufferPoolManager::GetPinCount(page_id_t page_id) -> std::optional<size_t> {
  auto to_table_shard{page_table_.VisitShard(page_id)};
  frame_id_t frame_id{INVALID_FRAME_ID};
  {
    std::lock_guard lk{to_table_shard->m_};
    auto to_map{&to_table_shard->map_};
    auto iter{to_map->find(page_id)};
    if (iter == to_map->end()) {
      return std::nullopt;
    }
    frame_id = iter->second;
    return replacer_->GetPinCnt(frame_id);
  }
}

}  // namespace bustub
