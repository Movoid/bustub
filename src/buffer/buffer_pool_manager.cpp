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
 * `pin_count_` 作为关键数据应该手动处理.
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
      bpm_latch_(std::make_shared<std::shared_mutex>()),
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
 *
 * Support callback.
 */
BufferPoolManager::~BufferPoolManager() {
  while (!callback_.empty()) {
    callback_.back()();
    callback_.pop_back();
  }
}

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

/**
 * `Schedule` 会对 frame data 做一份拷贝, 以实现 async.
 * 不会修改原本 frame data.
 */
auto BufferPoolManager::FlushPageAsyncUnsafe(page_id_t page_id, frame_id_t frame_id) -> std::future<bool> {
  FrameHeader &frame{*frames_[frame_id]};
  auto p{disk_scheduler_->CreatePromise()};
  std::future<bool> fut{p.get_future()};
  DiskRequest req{true, frame.GetDataMut(), page_id, std::move(p)};
  disk_scheduler_->Schedule(std::move(req));
  return fut;
}

/**
 * 重构后, 分配一个可用的 frame.
 */
auto BufferPoolManager::TryAllocateFrameUnsafe(frame_id_t &frame_id) -> bool {
  if (!free_frames_.empty()) {
    frame_id = free_frames_.back();
    free_frames_.pop_back();
    return true;
  }
  auto evicted{replacer_->Evict()};
  if (!evicted.has_value()) {
    frame_id = INVALID_FRAME_ID;
    return false;
  }
  frame_id = evicted.value();
  FrameHeader &frame{*frames_[frame_id]};
  /**
   * 此处应该能独占 frame, 无需 frame 锁.
   * 必须要求 pin 的生命周期长于 frame lk.
   * `frame.pin_count_` 实际被 bpm lk 保护.
   */
  BUSTUB_ASSERT(frame.pin_count_ == 0, "Unexpected.");

  /**
   * 由 DiskScheduler 保证在此之后的 Fetch 必须获得晚于此 Flush 的数据.
   */

  /**
   * 此处需要改进. Flush 应该在持有 frame lk 时执行.
   */
  if (frame.is_dirty_) {
    FlushPageAsyncUnsafe(frame.page_id_, frame_id);
  }

  page_table_.erase(frame.page_id_);
  frame_id = frame.frame_id_;
  frame.Reset();

  return true;
}

auto BufferPoolManager::TryCommitFrameUnsafe(frame_id_t &expected_frame_id, frame_id_t desired_frame_id,
                                             page_id_t page_id) -> bool {
  auto iter{page_table_.find(page_id)};
  if (expected_frame_id == INVALID_FRAME_ID) {
    if (iter != page_table_.end()) {
      expected_frame_id = iter->second;
      return false;
    }
  } else if (iter->second != expected_frame_id) {
    expected_frame_id = iter->second;
    return false;
  }

  page_table_.emplace(page_id, desired_frame_id);
  // 让上层构造 PageGuard 时 record.
  // replacer_->RecordAccess(frame_id);
  return true;
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
  /**
   * 必须调用 `DiskManager` 的 `WritePage`.
   * 否则 `ReadPage` 报错 page not in range.
   */
  page_id_t new_page_id{next_page_id_.fetch_add(1, std::memory_order_relaxed)};

  auto p{disk_scheduler_->CreatePromise()};
  DiskRequest req{true, nullptr, new_page_id, std::move(p), std::nullopt};
  disk_scheduler_->Schedule(std::move(req));

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
  frame_id_t frame_id{INVALID_FRAME_ID};
  {
    std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
    // frame_id_t frame_id{FindFrameUnsafe(page_id)};
    frame_id = FindFrameUnsafe(page_id);
    if (frame_id == INVALID_FRAME_ID) {
      disk_scheduler_->DeallocatePage(page_id);
      return true;
    }
    FrameHeader &frame{*frames_[frame_id]};
    if (frame.pin_count_ > 0) {
      return false;
    }
    replacer_->Remove(frame_id);
    page_table_.erase(page_id);
    // 独占.
    frame.Reset();
    free_frames_.emplace_back(frame_id);
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
    bool need_load{};
    {
      std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
      frame_id = FindFrameUnsafe(page_id);
      if (frame_id != INVALID_FRAME_ID) {  // In memory.
        FrameHeader &frame{*frames_[frame_id]};
        if (frame.pin_count_++ == 0) {
          replacer_->SetEvictable(frame_id, false);
        }
      } else {
        if (!TryAllocateFrameUnsafe(frame_id)) {
          return std::nullopt;
        }
        need_load = true;
      }
    }

    BUSTUB_ASSERT(frame_id != INVALID_FRAME_ID, "Unexpected.");

    if (need_load) {
      {
        std::lock_guard<std::shared_mutex> frame_lk{frames_[frame_id]->rwlatch_};
        FrameHeader &frame{*frames_[frame_id]};
        /**
         * 在此处立刻向 BPM 注册.
         * 注册成功后, 才开始 IO, 并始终持有 frame lk.
         */
        {
          std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
          // BUSTUB_ASSERT(frame.pin_count_ == 0, "Unexpected.");
          /**
           * 此处可能 `frame.pin_count_ > 0`.
           * `free_frames_` 中的 frame 可能仍被 pinned, 不影响正确性.
           */
          frame_id_t expected_frame_id{INVALID_FRAME_ID};
          if (!TryCommitFrameUnsafe(expected_frame_id, frame_id, page_id)) {
            /**
             * 另一个线程抢先注册了 page id.
             */
            free_frames_.emplace_back(frame_id);  // 回退到 `free_frames_` 中.
            frame.Reset();
            continue;
          }
          if (frame.pin_count_++ == 0) {
            replacer_->SetEvictable(frame_id, false);
          }
        }
        /**
         * Committed & Pinned. 开始 IO.
         */
        frame.page_id_ = page_id;
        FetchPageAsyncUnsafe(page_id, frame_id).get();
      }
    }

    return WritePageGuard{page_id, frames_[frame_id], replacer_, bpm_latch_};
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
    bool need_load{};
    {
      std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
      frame_id = FindFrameUnsafe(page_id);
      if (frame_id != INVALID_FRAME_ID) {  // In memory.
        FrameHeader &frame{*frames_[frame_id]};
        if (frame.pin_count_++ == 0) {
          replacer_->SetEvictable(frame_id, false);
        }
      } else {
        if (!TryAllocateFrameUnsafe(frame_id)) {
          return std::nullopt;
        }
        need_load = true;
      }
    }

    BUSTUB_ASSERT(frame_id != INVALID_FRAME_ID, "Unexpected.");

    if (need_load) {
      {
        std::lock_guard<std::shared_mutex> frame_lk{frames_[frame_id]->rwlatch_};
        FrameHeader &frame{*frames_[frame_id]};
        /**
         * 在此处立刻向 BPM 注册.
         * 注册成功后, 才开始 IO, 并始终持有 frame lk.
         */
        {
          std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
          // BUSTUB_ASSERT(frame.pin_count_ == 0, "Unexpected.");
          frame_id_t expected_frame_id{INVALID_FRAME_ID};
          if (!TryCommitFrameUnsafe(expected_frame_id, frame_id, page_id)) {
            /**
             * 另一个线程抢先注册了 page id.
             */
            free_frames_.emplace_back(frame_id);  // 回退到 `free_frames_` 中.
            frame.Reset();
            continue;
          }
          if (frame.pin_count_++ == 0) {
            replacer_->SetEvictable(frame_id, false);
          }
        }
        /**
         * Committed & Pinned. 开始 IO.
         */
        frame.page_id_ = page_id;
        FetchPageAsyncUnsafe(page_id, frame_id).get();
      }
    }

    return ReadPageGuard{page_id, frames_[frame_id], replacer_, bpm_latch_};
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
  {
    std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
    frame_id = FindFrameUnsafe(page_id);
    if (frame_id == INVALID_FRAME_ID) {
      return false;
    }
    FrameHeader &frame{*frames_[frame_id]};
    if (frame.pin_count_++ == 0) {
      replacer_->SetEvictable(frame_id, false);
    }
  }

  // {
  // std::shared_lock<std::shared_mutex> frame_lk{frames_[frame_id]->rwlatch_};
  FrameHeader &frame{*frames_[frame_id]};
  if (frame.is_dirty_) {
    FlushPageAsyncUnsafe(page_id, frame_id);
    frame.is_dirty_ = false;
  }
  {
    std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
    if (frame.pin_count_-- == 1) {
      replacer_->SetEvictable(frame_id, true);
    }
  }
  // }
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
  for (auto &frame_ptr : frames_) {
    {
      std::shared_lock<std::shared_mutex> frame_guard{frame_ptr->rwlatch_};
      if (frame_ptr->page_id_ == INVALID_PAGE_ID || !frame_ptr->is_dirty_) {
        continue;
      }
      FlushPageAsyncUnsafe(frame_ptr->page_id_, frame_ptr->frame_id_);
      frame_ptr->is_dirty_ = false;
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
  {
    std::lock_guard<std::shared_mutex> bpm_lk{*bpm_latch_};
    frame_id_t frame_id{FindFrameUnsafe(page_id)};
    if (frame_id == INVALID_FRAME_ID) {
      return std::nullopt;
    }
    FrameHeader &frame{*frames_[frame_id]};
    return frame.pin_count_;
  }
}

void BufferPoolManager::SetCallbackOnDestroyUnsafe(const std::function<void()> &func) { callback_.emplace_back(func); }

}  // namespace bustub
