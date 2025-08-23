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

#include "buffer/buffer_pool_manager.h"

// #define CUSTOM_LOG
// #include "common/custom_debug.h"

namespace bustub {

/**
 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
 *
 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
 *
 * @param frame_id The frame ID / index of the frame we are creating a header for.
 */
FrameHeader::FrameHeader(frame_id_t frame_id) : frame_id_(frame_id), data_(BUSTUB_PAGE_SIZE, 0) { Reset(); }

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
 *
 * 除了 `pin_count_` 全部清除. `pin_count_` 和 `replacer_` 同步.
 */
void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  is_dirty_ = false;
  page_id_ = INVALID_PAGE_ID;
}

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
      bpm_latch_(std::make_shared<std::mutex>()),
      replacer_(std::make_shared<LRUKReplacer>(num_frames, k_dist)),
      disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)),
      log_manager_(log_manager) {
  // Not strictly necessary...
  std::scoped_lock latch(*bpm_latch_);

  // Initialize the monotonically increasing counter at 0.
  next_page_id_.store(0);

  // Allocate all of the in-memory frames up front.
  frames_.reserve(num_frames_);

  // The page table should have exactly `num_frames_` slots, corresponding to exactly `num_frames_` frames.
  page_table_.reserve(num_frames_);

  // Initialize all of the frame headers, and fill the free frame list with all possible frame IDs (since all frames are
  // initially free).
  for (size_t i = 0; i < num_frames_; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(static_cast<int>(i));
  }
}

/**
 * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
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
auto BufferPoolManager::FetchPageUnsafe(page_id_t page_id, frame_id_t frame_id) -> bool {
  FrameHeader &frame{*frames_[frame_id]};
  auto p{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{p.get_future()};
  DiskRequest req{false, frame.GetDataMut(), page_id, std::move(p)};
  disk_scheduler_->Schedule(std::move(req));
  fut.get();
  return true;
}

/**
 * @brief Helper. 将一个 memory frame 数据写到 disk page.
 *
 * 写 disk.
 */
auto BufferPoolManager::FlushPageUnsafe(page_id_t page_id, frame_id_t frame_id) -> bool {
  FrameHeader &frame{*frames_[frame_id]};
  auto p{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{p.get_future()};
  DiskRequest req{true, frame.GetDataMut(), page_id, std::move(p)};
  disk_scheduler_->Schedule(std::move(req));
  fut.get();
  frame.is_dirty_ = false;
  return true;
}

auto BufferPoolManager::FetchPageAsyncUnsafe(page_id_t page_id, frame_id_t frame_id) -> std::future<bool> {
  FrameHeader &frame{*frames_[frame_id]};
  auto p{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{p.get_future()};
  DiskRequest req{false, frame.GetDataMut(), page_id, std::move(p)};
  disk_scheduler_->Schedule(std::move(req));
  return fut;
}

auto BufferPoolManager::FlushPageAsyncUnsafe(page_id_t page_id, frame_id_t frame_id) -> std::future<bool> {
  FrameHeader &frame{*frames_[frame_id]};
  auto p{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{p.get_future()};
  DiskRequest req{true, frame.GetDataMut(), page_id, std::move(p)};
  disk_scheduler_->Schedule(std::move(req));
  return fut;
}

auto BufferPoolManager::AcquireNewFrameUnsafe() -> frame_id_t {
  frame_id_t frame_id{INVALID_FRAME_ID};
  if (free_frames_.empty()) {  // Evict.
    auto evicted{replacer_->Evict()};
    if (!evicted.has_value()) {
      return INVALID_FRAME_ID;
    }
    frame_id = evicted.value();
  } else {  // Reuse.
    frame_id = free_frames_.back();
    free_frames_.pop_back();
  }
  return frame_id;
}

void BufferPoolManager::ReleaseNewFrameUnsafe(page_id_t old_page_id, page_id_t page_id, frame_id_t frame_id,
                                              bool set_evictable) {
  if (old_page_id != INVALID_PAGE_ID) {
    page_table_.erase(old_page_id);
    replacer_->Remove(frame_id);
  }
  page_table_.emplace(page_id, frame_id);
  if (set_evictable) {
    replacer_->SetEvictable(frame_id, true);
  }
}

auto BufferPoolManager::FindFrameUnsafe(page_id_t page_id) -> frame_id_t {
  auto pt_iter{page_table_.find(page_id)};
  return pt_iter == page_table_.end() ? INVALID_FRAME_ID : pt_iter->second;
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  // DiskManager 接口限制, 必须将新的 disk page 存入一个 memory frame.

  // DebugLog("Call\t\t NewPage()");

  page_id_t page_id{next_page_id_.fetch_add(1, std::memory_order_relaxed)};
  frame_id_t frame_id{INVALID_FRAME_ID};
  page_id_t old_page_id{INVALID_PAGE_ID};
  {
    std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
    frame_id = AcquireNewFrameUnsafe();
    if (frame_id == INVALID_FRAME_ID) {
      // DebugLog("Failed\t\t NewPage() : {}", "Not enough memory.");
      return INVALID_PAGE_ID;
    }
  }
  {
    std::lock_guard<std::shared_mutex> frame_guard{frames_[frame_id]->rwlatch_};
    FrameHeader &frame{*frames_[frame_id]};
    old_page_id = frame.page_id_;
    if (frame.page_id_ != INVALID_PAGE_ID && frame.is_dirty_) {
      FlushPageUnsafe(frame.page_id_, frame_id);
    }
    {
      std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
      ReleaseNewFrameUnsafe(old_page_id, page_id, frame_id, true);  // Evictable
    }
    frame.Reset();
    frame.page_id_ = page_id;
  }

  // DebugLog("OK\t\t NewPage() : {}", page_id);
  return page_id;
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t DeletePage({})", page_id);

  while (true) {
    frame_id_t frame_id{INVALID_FRAME_ID};
    {
      std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
      frame_id = FindFrameUnsafe(page_id);
      if (frame_id == INVALID_FRAME_ID) {
        disk_scheduler_->DeallocatePage(page_id);
        // DebugLog("OK\t\t DeletePage({}) : {}", page_id, "Deallocated but not in mem.");
        return true;
      }
      FrameHeader &frame{*frames_[frame_id]};
      // if (frame.pin_count_.load(std::memory_order_relaxed) > 0) {
      //   return false;
      // }
      if (frame.pin_count_ > 0) {
        return false;
      }
    }
    {
      std::lock_guard<std::shared_mutex> frame_guard{frames_[frame_id]->rwlatch_};
      FrameHeader &frame{*frames_[frame_id]};
      {
        std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
        frame_id_t check_id{FindFrameUnsafe(page_id)};
        // Validation.
        if (frame_id != check_id) {
          // DebugLog("!!! DeletePage({}) fallback", page_id);
          continue;
        }
        // if (frame.pin_count_.load(std::memory_order_relaxed) > 0) {
        //   return false;
        // }
        if (frame.pin_count_ > 0) {
          return false;
        }

        replacer_->Remove(frame_id);
        page_table_.erase(page_id);
        free_frames_.emplace_back(frame_id);
        disk_scheduler_->DeallocatePage(page_id);
      }
      frame.Reset();
    }
    // DebugLog("OK\t\t DeletePage({}) : {}", page_id, "Deleted.");
    return true;
  }
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t CheckedWritePage({})", page_id);

  while (true) {
    frame_id_t frame_id{INVALID_FRAME_ID};
    page_id_t old_page_id{INVALID_PAGE_ID};
    bool is_acquired{};
    {
      std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
      frame_id = FindFrameUnsafe(page_id);

      if (frame_id == INVALID_FRAME_ID) {
        frame_id = AcquireNewFrameUnsafe();
        if (frame_id == INVALID_FRAME_ID) {
          // DebugLog("Fail\t\t CheckedWritePage({}) : {}", page_id, "Not enough memory.");
          return std::nullopt;
        }
        is_acquired = true;
        // DebugLog("!!! CheckedWritePage({}) cache missed.", page_id);
      }
      FrameHeader &frame{*frames_[frame_id]};
      if (!is_acquired) {
        // if (frame.pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
        //   replacer_->SetEvictable(frame_id, false);
        // }
        if (frame.pin_count_++ == 0) {
          replacer_->SetEvictable(frame_id, false);
        }
      }
    }

    if (is_acquired) {
      // DebugLog("Info CheckedWritePage({}) lock frame.", page_id);
      {
        std::lock_guard<std::shared_mutex> frame_guard{frames_[frame_id]->rwlatch_};
        FrameHeader &frame{*frames_[frame_id]};
        old_page_id = frame.page_id_;
        // 此处必须 Flush
        if (frame.page_id_ != INVALID_PAGE_ID && frame.is_dirty_) {
          FlushPageUnsafe(frame.page_id_, frame_id);
        }
        frame.is_dirty_ = false;
        // DebugLog("Info CheckedWritePage({}) lock bpm.", page_id);
        {
          std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
          // if (frame.pin_count_.load(std::memory_order_relaxed) > 0) {
          //   // DebugLog("!!! CheckedWritePage({}) fallback", page_id);
          //   continue;
          // }
          if (frame.pin_count_ > 0) {
            continue;
          }
          frame_id_t check_id{FindFrameUnsafe(page_id)};
          if (check_id != INVALID_FRAME_ID && check_id != frame_id) {
            // replacer_->SetEvictable(frame_id, true);
            // continue;
            if (frame.page_id_ == INVALID_PAGE_ID) {  // 来自 `free_frames_`
              free_frames_.emplace_back(frame_id);
            } else {  // 来自 Evict
              replacer_->SetEvictable(frame_id, true);
            }
            // DebugLog("!!! CheckedWritePage({}) fallback", page_id);
            continue;
          }
          // DebugLog("Info CheckedWritePage({}) release new frame.", page_id);
          ReleaseNewFrameUnsafe(old_page_id, page_id, frame_id, false);
          // if (frame.pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
          //   replacer_->SetEvictable(frame_id, false);
          // }
          if (frame.pin_count_++ == 0) {
            replacer_->SetEvictable(frame_id, false);
          }
          // DebugLog("Info CheckedWritePage({}) record access.", page_id);
        }
        frame.page_id_ = page_id;
        // DebugLog("Info CheckedWritePage({}) fetching.", page_id);
        FetchPageUnsafe(page_id, frame_id);
      }
    }

    replacer_->RecordAccess(frame_id);
    // DebugLog("Info CheckedWritePage({}) construct guard.", page_id);
    WritePageGuard pg{page_id, frames_[frame_id], replacer_, bpm_latch_};

    // DebugLog("OK\t\t CheckedWritePage({})", page_id);
    return std::make_optional(std::move(pg));
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t CheckedReadPage({})", page_id);

  while (true) {
    frame_id_t frame_id{INVALID_FRAME_ID};
    page_id_t old_page_id{INVALID_PAGE_ID};
    bool is_acquired{};
    {
      std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
      frame_id = FindFrameUnsafe(page_id);
      if (frame_id == INVALID_FRAME_ID) {
        frame_id = AcquireNewFrameUnsafe();
        if (frame_id == INVALID_FRAME_ID) {
          // DebugLog("Failed\t\t CheckedReadPage({}) : {}", page_id, "Not enough memory.");
          return std::nullopt;
        }
        is_acquired = true;
      }
      FrameHeader &frame{*frames_[frame_id]};
      if (!is_acquired) {
        // if (frame.pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
        //   replacer_->SetEvictable(frame_id, false);
        // }
        if (frame.pin_count_++ == 0) {
          replacer_->SetEvictable(frame_id, false);
        }
      }
    }

    if (is_acquired) {
      {
        std::lock_guard<std::shared_mutex> frame_guard{frames_[frame_id]->rwlatch_};
        FrameHeader &frame{*frames_[frame_id]};
        old_page_id = frame.page_id_;
        if (frame.page_id_ != INVALID_PAGE_ID && frame.is_dirty_) {
          FlushPageUnsafe(frame.page_id_, frame_id);
        }
        frame.is_dirty_ = false;
        {
          std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
          // 防止当前 frame 对应 page 已被另一线程 Pin, 不可换出
          // if (frame.pin_count_.load(std::memory_order_relaxed) > 0) {
          //   // DebugLog("!!! CheckedReadPage({}) fallback", page_id);
          //   continue;
          // }
          if (frame.pin_count_ > 0) {
            continue;
          }
          // 防止当前 page 已经被另一线程换入到已有的 frame 里
          frame_id_t check_id{FindFrameUnsafe(page_id)};
          if (check_id != INVALID_FRAME_ID && check_id != frame_id) {
            // replacer_->SetEvictable(frame_id, true);
            // continue;
            if (frame.page_id_ == INVALID_PAGE_ID) {  // 来自 `free_frames_`
              free_frames_.emplace_back(frame_id);
            } else {  // 来自 Evict
              replacer_->SetEvictable(frame_id, true);
            }
            // DebugLog("!!! CheckedReadPage({}) fallback", page_id);
            continue;
          }
          ReleaseNewFrameUnsafe(old_page_id, page_id, frame_id, false);
          // if (frame.pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
          //   replacer_->SetEvictable(frame_id, false);
          // }
          if (frame.pin_count_++ == 0) {
            replacer_->SetEvictable(frame_id, false);
          }
        }

        frame.page_id_ = page_id;
        FetchPageUnsafe(page_id, frame_id);
      }
    }

    replacer_->RecordAccess(frame_id);
    ReadPageGuard pg{page_id, frames_[frame_id], replacer_, bpm_latch_};
    // DebugLog("OK\t\t CheckedReadPage({})", page_id);
    return std::make_optional(std::move(pg));
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
 *
 * 调用前必须 lock frame.
 * 此函数不会死锁,
 * 前提是没有其他函数尝试嵌套 BufferPoolManager `bpm_latch_` + FrameHeader `rwlatch_`.
 */
auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t FlushPage({})", page_id);

  frame_id_t frame_id{INVALID_FRAME_ID};
  {
    std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
    frame_id = FindFrameUnsafe(page_id);
    if (frame_id == INVALID_FRAME_ID) {
      // DebugLog("Failed\t\t FlushPage({}) : {}", page_id, "Page is not in memory.");
      return false;
    }
  }

  // {
  // std::shared_lock<std::shared_mutex> frame_guard{frames_[frame_id]->rwlatch_};
  FrameHeader &frame{*frames_[frame_id]};
  FlushPageUnsafe(page_id, frame_id);
  frame.is_dirty_ = false;
  // DebugLog("OK\t\t FlushPage({}) : Data: {}", page_id, frame.GetData());
  return true;
  // }
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t FlushAllPages()");

  for (auto &frame_ptr : frames_) {
    {
      std::lock_guard<std::shared_mutex> frame_guard{frame_ptr->rwlatch_};
      if (frame_ptr->page_id_ == INVALID_PAGE_ID || !frame_ptr->is_dirty_) {
        continue;
      }
      FlushPageUnsafe(frame_ptr->page_id_, frame_ptr->frame_id_);
      frame_ptr->is_dirty_ = false;
    }
  }
  // DebugLog("OK\t\t FlushAllPages()");
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
  // UNIMPLEMENTED("TODO(P1): Add implementation.");

  // DebugLog("Call\t\t GetPinCount({})", page_id);

  {
    std::lock_guard<std::mutex> bpm_guard{*bpm_latch_};
    frame_id_t frame_id{FindFrameUnsafe(page_id)};
    if (frame_id == INVALID_FRAME_ID) {
      // DebugLog("Failed\t\t GetPinCount({}) : {}", page_id, "Page is not in memory.");

      return std::nullopt;
    }
    FrameHeader &frame{*frames_[frame_id]};
    // size_t res{frame.pin_count_.load(std::memory_order_relaxed)};
    size_t res{frame.pin_count_};
    // DebugLog("OK\t\t GetPinCount({})", page_id);

    return std::make_optional(res);
  }
}

}  // namespace bustub
