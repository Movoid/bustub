//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "buffer/lru_k_replacer.h"
#include "common/config.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

class BufferPoolManager;
class ReadPageGuard;
class WritePageGuard;

/**
 * @brief A helper class for `BufferPoolManager` that manages a frame of memory and related metadata.
 *
 * This class represents headers for frames of memory that the `BufferPoolManager` stores pages of data into. Note that
 * the actual frames of memory are not stored directly inside a `FrameHeader`, rather the `FrameHeader`s store pointer
 * to the frames and are stored separately them.
 *
 * ---
 *
 * Something that may (or may not) be of interest to you is why the field `data_` is stored as a vector that is
 * allocated on the fly instead of as a direct pointer to some pre-allocated chunk of memory.
 *
 * In a traditional production buffer pool manager, all memory that the buffer pool is intended to manage is allocated
 * in one large contiguous array (think of a very large `malloc` call that allocates several gigabytes of memory up
 * front). This large contiguous block of memory is then divided into contiguous frames. In other words, frames are
 * defined by an offset from the base of the array in page-sized (4 KB) intervals.
 *
 * In BusTub, we instead allocate each frame on its own (via a `std::vector<char>`) in order to easily detect buffer
 * overflow with address sanitizer. Since C++ has no notion of memory safety, it would be very easy to cast a page's
 * data pointer into some large data type and start overwriting other pages of data if they were all contiguous.
 *
 * If you would like to attempt to use more efficient data structures for your buffer pool manager, you are free to do
 * so. However, you will likely benefit significantly from detecting buffer overflow in future projects (especially
 * project 2).
 */
class FrameHeader {
  friend class BufferPoolManager;
  friend class ReadPageGuard;
  friend class WritePageGuard;

 public:
  explicit FrameHeader(frame_id_t frame_id);

 private:
  // FrameHeader 的接口全都 NOT thread-safe.
  // 因为 PageGuard 通常生命周期内都持有 `rwlatch`,
  // BufferPoolManager 需要调用前额外 lock `rwlatch_`.
  auto GetData() const -> const char *;
  auto GetDataMut() -> char *;
  void Reset();

  /** @brief The frame ID / index of the frame this header represents. */
  const frame_id_t frame_id_;

  /** @brief The readers / writer latch for this frame.
   *
   * 对 frame 加锁, 在 PageGuard 构造时, 就应该上锁,
   * PageGuard 析构时解锁.
   *
   * BufferPoolManager 也可能对此上锁, 在 evict page 时,
   * 需要读 frame 的 FrameHeader 来查询被 evict 的 page id.
   *
   */
  std::shared_mutex rwlatch_;

  /** @brief The number of pins on this frame keeping the page in memory.
   *
   * 注意, `pin_count_` 必须和 replacer_ `SetEvictable` 完全同步,
   * 且必须用 `bpm_latch_` 保护原子操作,
   * 用 frame `rwlatch_` 会导致需要检查 `pin_count_` 时重复上锁死锁.
   */
  std::atomic<size_t> pin_count_;

  /** @brief The dirty flag.
   *
   * dirty 标记, 写 frame 后需要标记, 在 frame evict 后需要 flush.
   *
   */
  bool is_dirty_;

  /**
   * @brief A pointer to the data of the page that this frame holds.
   *
   * If the frame does not hold any page data, the frame contains all null bytes.
   *
   * memory frame 的实际数据
   */
  std::vector<char> data_;

  /**
   * TODO(P1): You may add any fields or helper functions under here that you think are necessary.
   *
   * One potential optimization you could make is storing an optional page ID of the page that the `FrameHeader` is
   * currently storing. This might allow you to skip searching for the corresponding (page ID, frame ID) pair somewhere
   * else in the buffer pool manager...
   *
   * 当前 frame 存储的数据所属的 disk page.
   * 主要用于通过 frame 反查 page, 以便维护 BufferPoolManager 中的 `page_table_`.
   * 注意, page_id_ 为 0 时可能有效或无效,
   * 只有 `page_table_` 能通过 page id 0 映射到此 frame 才说明有效.
   */
  page_id_t page_id_{INVALID_PAGE_ID};
};

/**
 * @brief The declaration of the `BufferPoolManager` class.
 *
 * As stated in the writeup, the buffer pool is responsible for moving physical pages of data back and forth from
 * buffers in main memory to persistent storage. It also behaves as a cache, keeping frequently used pages in memory for
 * faster access, and evicting unused or cold pages back out to storage.
 *
 * Make sure you read the writeup in its entirety before attempting to implement the buffer pool manager. You also need
 * to have completed the implementation of both the `LRUKReplacer` and `DiskManager` classes.
 */
class BufferPoolManager {
 public:
  BufferPoolManager(size_t num_frames, DiskManager *disk_manager, size_t k_dist = LRUK_REPLACER_K,
                    LogManager *log_manager = nullptr);
  ~BufferPoolManager();

  auto Size() const -> size_t;
  auto NewPage() -> page_id_t;
  auto DeletePage(page_id_t page_id) -> bool;

  // 上层操作 BufferPoolManager 来获得 page 的 PageGuard,
  // 通过 PageGuard 对 frame 进行 thread-safe 读写.
  auto CheckedWritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown)
      -> std::optional<WritePageGuard>;
  auto CheckedReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> std::optional<ReadPageGuard>;
  auto WritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> WritePageGuard;
  auto ReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> ReadPageGuard;
  auto FlushPage(page_id_t page_id) -> bool;
  void FlushAllPages();
  auto GetPinCount(page_id_t page_id) -> std::optional<size_t>;

 private:
  auto FetchPageUnsafe(page_id_t page_id, frame_id_t frame_id) -> bool;
  auto FlushPageUnsafe(page_id_t page_id, frame_id_t frame_id) -> bool;

  auto FetchPageAsyncUnsafe(page_id_t page_id, frame_id_t frame_id) -> std::future<bool>;
  auto FlushPageAsyncUnsafe(page_id_t page_id, frame_id_t frame_id) -> std::future<bool>;

  auto AcquireNewFrameUnsafe() -> frame_id_t;
  void ReleaseNewFrameUnsafe(page_id_t old_page_id, page_id_t page_id, frame_id_t frame_id, bool set_evictable);
  auto FindFrameUnsafe(page_id_t page_id) -> frame_id_t;

 private:
  /** @brief The number of frames in the buffer pool.
   *
   * 构造时决定, 实际大小取决于 memory 大小.
   */
  const size_t num_frames_;

  /** @brief The next page ID to be allocated.  */
  std::atomic<page_id_t> next_page_id_;

  /**
   * @brief The latch protecting the buffer pool's inner data structures.
   *
   * BufferPool Manager Latch,
   * 在读写 BufferPoolManager 内
   * `page_table_`, `free_frames_`, `replacer_`(逻辑需要)
   * 时上锁.
   *
   * 读写 FrameHeader 中的 `page_id_` 使用此锁.
   *
   * TODO(P1) We recommend replacing this comment with details about what this latch actually protects.
   */
  std::shared_ptr<std::mutex> bpm_latch_;

  /** @brief The frame headers of the frames that this buffer pool manages.
   *
   * 维护所有的 frame.
   * 在 BufferPoolManager 构造后不会修改,
   * 访问 FrameHeader `data_` 等需要对 `rwlatch_` 上锁.
   *
   * thread-safe.
   */
  std::vector<std::shared_ptr<FrameHeader>> frames_;

  /** @brief The page table that keeps track of the mapping between pages and buffer pool frames.
   *
   * 维护每个 page 所在的 frame id.
   * 所有对 page 的 R/W 都需要经过 `page_table_` 寻址到 frame.
   *
   * NOT thread-safe.
   */
  std::unordered_map<page_id_t, frame_id_t> page_table_;

  /** @brief A list of free frames that do not hold any page's data.
   *
   * 所有可用的空 frame.
   * 如果此 list 为空, 则需要 replacer 淘汰 page 并复用 frame.
   *
   * NOT thread-safe.
   */
  std::list<frame_id_t> free_frames_;

  /** @brief The replacer to find unpinned / candidate pages for eviction.
   *
   * 负责维护 page 访问事件, 并用于 page evict 的 replacer.
   *
   * 注意每次 `Evict`, 被驱逐的 page 对应的 frame 应该被写盘.
   * 注意所有 frame 默认是不可驱逐的, 需要在合适的时机手动调整.
   *
   * 调用 `SetEvictable` 必须 lock `bpm_latch_` 来保证条件检查有效.
   *
   * thread-safe.
   */
  std::shared_ptr<LRUKReplacer> replacer_;

  /** @brief A pointer to the disk scheduler. */
  std::unique_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief A pointer to the log manager.
   *
   * Note: Please ignore this for P1.
   */
  LogManager *log_manager_ __attribute__((__unused__));

  /**
   * TODO(P1): You may add additional private members and helper functions if you find them necessary.
   *
   * There will likely be a lot of code duplication between the different modes of accessing a page.
   *
   * We would recommend implementing a helper function that returns the ID of a frame that is free and has nothing
   * stored inside of it. Additionally, you may also want to implement a helper function that returns either a shared
   * pointer to a `FrameHeader` that already has a page's data stored inside of it, or an index to said `FrameHeader`.
   */
};
}  // namespace bustub
