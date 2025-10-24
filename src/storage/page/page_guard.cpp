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

// #include "common/custom_debug.h"

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
 */
ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<ShardingLRUKReplacer> replacer)
    : page_id_(page_id), frame_(std::move(frame)), replacer_(std::move(replacer)), lock_holder_(frame_->rwlatch_) {
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
 *
 * 测试中存在 self assignment 测试.
 */
ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept {
  if (&that == this) {
    return;
  }
  page_id_ = that.page_id_;
  frame_ = that.frame_;
  replacer_ = that.replacer_;
  is_valid_ = that.is_valid_;
  lock_holder_ = std::move(that.lock_holder_);
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
  if (&that == this) {
    return *this;
  }
  Drop();
  page_id_ = that.page_id_;
  frame_ = that.frame_;
  replacer_ = that.replacer_;
  is_valid_ = that.is_valid_;
  lock_holder_ = std::move(that.lock_holder_);
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
  // DebugLog("OK\t\t Reader GetData of page {} : {}", page_id_, frame_->GetData());
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
  if (!is_valid_) {
    return;
  }
  /**
   * 在 `Unpin` 之前 unlock frame, 让状态更简单.
   * 在 `pin_cnt_ == 0` 时一定没有人持有 frame 锁.
   */
  lock_holder_.unlock();
  replacer_->Unpin(frame_->frame_id_);
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
 */
WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ShardingLRUKReplacer> replacer)
    : page_id_(page_id), frame_(std::move(frame)), replacer_(std::move(replacer)), lock_holder_(frame_->rwlatch_) {
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
  if (&that == this) {
    return;
  }
  page_id_ = that.page_id_;
  frame_ = that.frame_;
  replacer_ = that.replacer_;
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  lock_holder_ = std::move(that.lock_holder_);
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
  if (&that == this) {
    return *this;
  }
  Drop();
  page_id_ = that.page_id_;
  frame_ = that.frame_;
  replacer_ = that.replacer_;
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  lock_holder_ = std::move(that.lock_holder_);
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
  // DebugLog("OK\t\t Writer GetData of page {} : {}", page_id_, frame_->GetData());
  return frame_->GetData();
}

/**
 * @brief Gets a mutable pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");

  // DebugLog("OK\t\t Writer GetData of page {} : {}", page_id_, frame_->GetData());
  frame_->is_dirty_ = true;
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
 * @brief Manually drops a valid `WritePageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 * `Drop` 应该释放锁, 允许上层手动调用 `Drop`.
 */
void WritePageGuard::Drop() {
  if (!is_valid_) {
    return;
  }
  frame_id_t frame_id{frame_->frame_id_};
  lock_holder_.unlock();
  replacer_->Unpin(frame_id);

  is_valid_ = false;
}

/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
