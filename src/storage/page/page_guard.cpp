//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <memory>

namespace bustub {

/**
 * @brief The only constructor for an RAII `ReadPageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * PageGuard 的唯一合法构造函数,
 * default 构造函数仅用于允许提前构造再 move.
 * 后续所有操作都需要 `is_valid_`, 尤其是析构.
 *
 * PageGuard 在构造时应该对 `frame_` 上锁,
 * 无论是 R/W, 都需要阻止其他线程的访问.
 *
 * 此外, PageGuard 需要在构造时向 `replacer_` 注册,
 * 需要在特定操作 lock `bpm_latch_` .
 * 以及, 根据引用计数决定是否允许 `replacer_` evict.
 *
 * @param page_id The page ID of the page we want to read.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 * @param disk_scheduler A shard pointer to the buffer pool manager's disk scheduler.
 */
ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<LRUKReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                             std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),                     // FrameHeader, 用于获取数据, 维护 frame 引用计数.
      replacer_(std::move(replacer)),               // LRUKReplacer, 用于在上层访问 frame 时, 向 replacer 注册访问事件.
      bpm_latch_(std::move(bpm_latch)),             // 对 BufferPoolManager 加锁.
      disk_scheduler_(std::move(disk_scheduler)) {  // flush 需要向 disk scheduler 提交 DiskRequest .
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  // 锁 BPM
  {
    std::lock_guard<std::mutex> guard{*bpm_latch};
    replacer_->RecordAccess(frame_->frame_id_);
    if (frame_->pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
      replacer_->SetEvictable(frame_->frame_id_, false);
    }
  }
  // 锁 frame, Reader 锁
  frame_->rwlatch_.lock_shared();
  is_valid_ = true;
}

/**
 * @brief The move constructor for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 */
ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept {
  Drop();
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
}

/**
 * @brief The move assignment operator for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
 * holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return ReadPageGuard& The newly valid `ReadPageGuard`.
 */
auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  Drop();
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->GetData();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->is_dirty_;
}

/**
 * @brief Flushes this page's data safely to disk.
 *
 * 必须约定, 在 `Flush` 之后, 不允许再通过指针读写 frame 数据.
 * 否则 `is_dirty_` 标记出错.
 *
 * TODO(P1): Add implementation.
 */
void ReadPageGuard::Flush() {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  std::future<bool> fut{};
  {
    std::lock_guard<std::mutex> guard{*bpm_latch_};
    auto p{disk_scheduler_->CreatePromise()};
    fut = p.get_future();
    // flush 写磁盘
    DiskRequest req{true, frame_->GetDataMut(), page_id_, std::move(p)};
    disk_scheduler_->Schedule(std::move(req));
    replacer_->RecordAccess(frame_->frame_id_);
  }
  fut.get();
  frame_->is_dirty_ = false;
}

/**
 * @brief Manually drops a valid `ReadPageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * 析构 PageGuard, 需要减少引用计数, 释放 frame 锁.
 * Read 不会 dirty .
 *
 * TODO(P1): Add implementation.
 */
void ReadPageGuard::Drop() {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  if (!is_valid_) {
    return;
  }
  // 在此处 `unlock_shared`, 允许其他线程读写 frame.
  frame_->rwlatch_.unlock_shared();
  // `bpm_latch_` 存在 Synchronizes-with 作用, 不需要依赖 atomic.
  {
    std::lock_guard<std::mutex> guard{*bpm_latch_};
    if (frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
      // 注意此处可能的 Race: 另一个线程访问此 frame, 并设置 false evictable.
      // 通过 `bpm_latch_` 阻止.
      replacer_->SetEvictable(frame_->frame_id_, true);
    }
  }
  is_valid_ = false;
}

/** @brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. */
ReadPageGuard::~ReadPageGuard() { Drop(); }

/**********************************************************************************************************************/
/**********************************************************************************************************************/
/**********************************************************************************************************************/

/**
 * @brief The only constructor for an RAII `WritePageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to write to.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 * @param disk_scheduler A shared pointer to the buffer pool manager's disk scheduler.
 */
WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<LRUKReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  {
    std::lock_guard<std::mutex> guard{*bpm_latch};
    replacer_->RecordAccess(frame_->frame_id_);
    if (frame_->pin_count_.fetch_add(1, std::memory_order_relaxed) == 0) {
      replacer_->SetEvictable(frame_->frame_id_, false);
    }
  }
  // 锁 frame, Reader 锁

  frame_->rwlatch_.lock();
  is_valid_ = true;
}

/**
 * @brief The move constructor for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 */
WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept {
  Drop();
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
}

/**
 * @brief The move assignment operator for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are
 * many great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might
 * be holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return WritePageGuard& The newly valid `WritePageGuard`.
 */
auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  Drop();
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  disk_scheduler_ = std::move(that.disk_scheduler_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

/**
 * @brief Gets a mutable pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetDataMut();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

/**
 * @brief Flushes this page's data safely to disk.
 *
 * TODO(P1): Add implementation.
 *
 * 必须约定, 在 `Flush` 之后, 不允许再通过指针读写 frame 数据.
 * 否则 `is_dirty_` 标记出错.
 *
 */
void WritePageGuard::Flush() {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  std::future<bool> fut{};
  {
    std::lock_guard<std::mutex> guard{*bpm_latch_};
    auto p{disk_scheduler_->CreatePromise()};
    fut = p.get_future();
    // flush 写磁盘
    DiskRequest req{true, frame_->GetDataMut(), page_id_, std::move(p)};
    disk_scheduler_->Schedule(std::move(req));
    replacer_->RecordAccess(frame_->frame_id_);
  }
  fut.get();
  frame_->is_dirty_ = false;
}

/**
 * @brief Manually drops a valid `WritePageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 */
void WritePageGuard::Drop() {
  // UNIMPLEMENTED("TODO(P1): Add implementation.");
  if (!is_valid_) {
    return;
  }
  frame_->rwlatch_.unlock();
  {
    std::lock_guard<std::mutex> guard{*bpm_latch_};
    if (frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
      replacer_->SetEvictable(frame_->frame_id_, true);
    }
  }
  is_valid_ = false;
}

/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
