//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.h
//
// Identification: src/include/storage/disk/disk_scheduler.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <unordered_map>

#include "common/macros.h"
#include "readerwriterqueue/readerwriterqueue.h"
#include "storage/disk/disk_manager.h"

// ============================================================
//
// SimpleCU_QSBR.h
//
// ============================================================

namespace simple_cu::utils {

constexpr static std::size_t CACHELINE_SIZE{64};
inline std::size_t concurrency{std::thread::hardware_concurrency()};

/**
 * WARNING: Never use this class with polymorphism.
 * The base class may not have a `virtual` destructor.
 */
template <typename ValType, typename Requires = void>
struct Aligned {};

template <typename ValType>
struct alignas(CACHELINE_SIZE) Aligned<ValType, std::enable_if_t<std::is_class_v<ValType>>> : public ValType {
  using ValType::ValType;
  using ValType::operator=;

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_>>>
  explicit Aligned(ValType_ &&that) : ValType(std::forward<ValType_>(that)) {}
};

template <typename ValType>
struct alignas(CACHELINE_SIZE) Aligned<ValType, std::enable_if_t<!std::is_class_v<ValType>>> {
  ValType val_{};

  Aligned() = default;
  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_>>>
  explicit Aligned(ValType_ &&that) : ValType(std::forward<ValType_>(that)) {}
  auto operator=(const ValType &v) -> Aligned & {
    val_ = v;
    return *this;
  }
  explicit operator ValType &() { return val_; }
  explicit operator const ValType &() const { return val_; }
};

template <typename ValType>
using DefaultDeleter = std::default_delete<std::remove_pointer_t<ValType>>;

template <typename DeleterType, typename Requires = void>
class EBODeleterStorage {
 private:
  DeleterType deleter_;

 public:
  EBODeleterStorage() = default;  // 如果非空 DeleterType 能空参构造, 则也允许
  explicit EBODeleterStorage(const DeleterType &deleter) : deleter_{deleter} {  // 使用一个 DeleterType 对象构造
  }
  EBODeleterStorage(const EBODeleterStorage &) = default;
  EBODeleterStorage(EBODeleterStorage &&) noexcept = default;
  auto operator=(const EBODeleterStorage &) -> EBODeleterStorage & = default;
  auto operator=(EBODeleterStorage &&) noexcept -> EBODeleterStorage & = default;
  ~EBODeleterStorage() = default;

  auto GetDeleter() -> DeleterType & { return deleter_; }
};

template <typename DeleterType>
class EBODeleterStorage<DeleterType, std::enable_if_t<std::is_empty_v<DeleterType>>> : public DeleterType {
 public:
  EBODeleterStorage() = default;  // 空的 DeleterType 通常可以空参构造
  explicit EBODeleterStorage(const DeleterType &deleter)
      : DeleterType{deleter} {  // 也允许使用一个已经构造了的空 DeleterType.
  }
  EBODeleterStorage(const EBODeleterStorage &) = default;
  EBODeleterStorage(EBODeleterStorage &&) noexcept = default;
  auto operator=(const EBODeleterStorage &) -> EBODeleterStorage & = default;
  auto operator=(EBODeleterStorage &&) noexcept -> EBODeleterStorage & = default;
  ~EBODeleterStorage() = default;

  auto GetDeleter() -> DeleterType & { return *this; }
};

/**
 * 极简并发 Queue.
 */
template <typename ValType, typename AllocType = std::allocator<ValType>>
class SimpleConcurrentQueue {
 public:
  SimpleConcurrentQueue() = default;
  ~SimpleConcurrentQueue() = default;

  void Push(ValType element) {
    std::unique_lock lk{m_};
    q_.emplace(std::move(element));
    cv_.notify_one();
  }

  auto Pop() -> ValType {
    std::unique_lock lk{m_};
    cv_.wait(lk, [&]() { return !q_.empty(); });
    ValType element{std::move(q_.front())};
    q_.pop();
    return element;
  }

  auto TryPop(ValType &ret) -> bool {
    std::unique_lock lk{m_};
    if (q_.empty()) {
      return false;
    }
    ret = std::move(q_.front());
    q_.pop();
    return true;
  }

  template <typename AllocType_ = std::allocator<ValType>>
  auto PopAll(std::queue<ValType, std::deque<ValType, AllocType>> &ret) -> bool {
    std::unique_lock lk{m_};
    q_.swap(ret);
    return true;
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::queue<ValType, std::deque<ValType, AllocType>> q_;
};

}  // namespace simple_cu::utils

namespace simple_cu::qsbr::details {

template <typename ValType, typename DeleterType>
class RetiredContext {
 private:
  using masked_epoch_t = std::uint16_t;
  using ctx_idx_t_ = std::uint16_t;
  using CriticalEpochSnapshot_ = std::vector<std::pair<ctx_idx_t_, masked_epoch_t>>;

  template <std::size_t Size>
  using FullEpochSnapshot_ = std::array<masked_epoch_t, Size>;

  struct RetiredNode {
    ValType val_;
    CriticalEpochSnapshot_ critical_snapshot_;
    RetiredNode *next_;
  };

  RetiredNode *retired_{};
  std::atomic<std::uint64_t> cnt_{};

 public:
  RetiredContext() = default;
  ~RetiredContext() {
    RetiredNode *old_retired{retired_};
    BUSTUB_ASSERT(old_retired == nullptr, "Detected alive active thread.");
  }
  RetiredContext(const RetiredContext &obj) = delete;
  auto operator=(const RetiredContext &obj) -> RetiredContext & = delete;
  RetiredContext(RetiredContext &&obj) = delete;
  auto operator=(RetiredContext &&obj) -> RetiredContext & = delete;

  auto GetCnt() -> std::uint64_t { return cnt_.load(std::memory_order_relaxed); }

  void Retire(ValType &&val, CriticalEpochSnapshot_ &&snapshot) {
    RetiredNode *new_node{new RetiredNode{std::move(val), std::move(snapshot), retired_}};
    retired_ = new_node;
    cnt_.fetch_add(1, std::memory_order_relaxed);
  }

  template <std::size_t Size, typename DeleterType_ = DeleterType>
  void Reclaim(const FullEpochSnapshot_<Size> &latest_snapshot, DeleterType_ &&deleter) {
    RetiredNode *old_retired{retired_};
    std::uint64_t unsafe_cnt{};
    retired_ = nullptr;
    cnt_.store(0, std::memory_order_relaxed);
    while (old_retired) {
      RetiredNode *next{old_retired->next_};
      bool is_unsafe{};
      for (auto &rec : old_retired->critical_snapshot_) {
        if (latest_snapshot[rec.first] == rec.second) {  // 若 Epoch 为奇数且没变则 unsafe
          is_unsafe = true;
          break;
        }
      }
      if (is_unsafe) {
        old_retired->next_ = retired_;
        retired_ = old_retired;
        unsafe_cnt++;
      } else {
        std::forward<DeleterType_>(deleter)(std::move(old_retired->val_));
        delete old_retired;
      }
      old_retired = next;
    }
    cnt_.fetch_add(unsafe_cnt, std::memory_order_relaxed);
  }
};

};  // namespace simple_cu::qsbr::details

namespace simple_cu::qsbr {

/**
 * @brief Quiescent-State Based Reclamation.
 *
 * @tparam ThreadCnt 最大线程数.
 * @tparam ValType 受管理的确切类型.
 * @tparam DeleterType 自定义 deleter.
 */
template <std::size_t ThreadCnt, typename ValType, typename DeleterType = utils::DefaultDeleter<ValType>>
class QSBRManager : private utils::EBODeleterStorage<DeleterType> {
 private:
  static_assert(ThreadCnt <= std::numeric_limits<std::uint16_t>::max(), "Too many threads.");
  using DeleterStorage_ = utils::EBODeleterStorage<DeleterType>;

  using epoch_t_ = std::uint32_t;
  using masked_epoch_t = std::uint16_t;
  using ctx_idx_t_ = std::uint16_t;
  using mgr_idx_t_ = std::uint64_t;

  using Epoch_ = std::atomic<epoch_t_>;
  using RetiredContext_ = details::RetiredContext<ValType, DeleterType>;
  using QSBRContext_ = utils::Aligned<std::pair<Epoch_, RetiredContext_>>;

  using CriticalEpochSnapshot_ = std::vector<std::pair<ctx_idx_t_, masked_epoch_t>>;

  template <std::size_t Size>
  using FullEpochSnapshot_ = std::array<masked_epoch_t, Size>;

  struct LocalEntry {
    QSBRContext_ *local_qsbr_ctx_;
  };

  struct CallbackContext {
    std::vector<std::function<void()>> callbacks_;
    ~CallbackContext() {
      while (!callbacks_.empty()) {
        callbacks_.back()();
        callbacks_.pop_back();
      }
    }
  };

  struct QSBRLifetimeControlBlock {
    std::atomic<bool> alive_{true};
  };

  constexpr static std::uint64_t EPOCH_MASK{(1ULL << 16) - 1};

  /**
   * `Epoch_` 和 `RetiredContext_` 共用 Cacheline .
   */
  std::unique_ptr<std::array<QSBRContext_, ThreadCnt>> ctxs_;
  std::atomic<ctx_idx_t_> next_ctx_idx_{};

  inline static std::atomic<mgr_idx_t_> next_mgr_idx{};
  const mgr_idx_t_ mgr_idx_;
  thread_local inline static std::unordered_map<mgr_idx_t_, LocalEntry> tls_map;

  /**
   * 线程结束的回调机制, 用于清理残余的所有资源.
   * 非 Thread-Safe.
   * 注意定义顺序决定析构顺序.
   */
  std::shared_ptr<QSBRLifetimeControlBlock> ctrl_{std::make_shared<QSBRLifetimeControlBlock>()};
  std::atomic<ctx_idx_t_> active_thread_cnt_{};
  std::shared_mutex register_mtx_;
  thread_local inline static CallbackContext callback_on_thread_exit;

  /** 奇数 Epoch 则此线程位于临界区. */
  auto IsCriticalEpoch(epoch_t_ epoch) -> bool { return (epoch & 1) == 1; }

  auto GetContext() -> std::optional<LocalEntry> {
    auto iter{tls_map.find(mgr_idx_)};
    if (iter != tls_map.end()) {
      return std::make_optional(iter->second);
    }
    ctx_idx_t_ cur_ctx_idx{next_ctx_idx_.load(std::memory_order_relaxed)};
    {
      // Register this new thread.
      std::shared_lock<std::shared_mutex> register_lk{register_mtx_};
      do {
        if (cur_ctx_idx >= ThreadCnt) {
          return std::nullopt;
        }
      } while (!next_ctx_idx_.compare_exchange_weak(cur_ctx_idx, cur_ctx_idx + 1, std::memory_order_release,
                                                    std::memory_order_relaxed));
      active_thread_cnt_.fetch_add(1, std::memory_order_release);
    }
    // Registered.
    tls_map[mgr_idx_] = LocalEntry{&(*ctxs_)[cur_ctx_idx]};

    // Register callback for this QSBR instance.
    callback_on_thread_exit.callbacks_.emplace_back([this, weak = std::weak_ptr<QSBRLifetimeControlBlock>{ctrl_}] {
      if (auto ptr = weak.lock(); !ptr || !ptr->alive_.load(std::memory_order_acquire)) {
        return;
      }
      this->ReclaimLocal();
      {
        std::unique_lock<std::shared_mutex> register_lk{register_mtx_};
        if (this->active_thread_cnt_.fetch_sub(1, std::memory_order_acq_rel) <= 2) {
          this->ReclaimGlobalUnsafe();
        }
      }
    });

    return tls_map[mgr_idx_];
  }

  auto SnapshotCriticalEpochs() -> CriticalEpochSnapshot_ {
    ctx_idx_t_ end_idx{next_ctx_idx_.load(std::memory_order_acquire)};
    CriticalEpochSnapshot_ snapshot{};
    snapshot.reserve(end_idx / 2);
    for (ctx_idx_t_ i = 0; i < end_idx; i++) {
      masked_epoch_t epoch_i{
          static_cast<masked_epoch_t>((*ctxs_)[i].first.load(std::memory_order_acquire) & EPOCH_MASK)};
      if (IsCriticalEpoch(epoch_i)) {
        snapshot.emplace_back(std::make_pair(i, epoch_i));
      }
    }
    return snapshot;
  }

  /**
   * 栈上分配定长的 `FullEpochSnapshot_<ThreadCnt>` 避免 `new` 带来的锁开销.
   * 全量快照避免某些奇怪的 Race,
   * 若只 snapshot [0, `end_idx`),
   * 不能保证其他线程 CriticalEpochs 有比 `end_idx` 更大的记录.
   */
  auto SnapshotFullEpochs() -> FullEpochSnapshot_<ThreadCnt> {
    FullEpochSnapshot_<ThreadCnt> snapshot{};
    for (ctx_idx_t_ i = 0; i < static_cast<ctx_idx_t_>(ThreadCnt); i++) {
      snapshot[i] = static_cast<masked_epoch_t>((*ctxs_)[i].first.load(std::memory_order_acquire) & EPOCH_MASK);
    }
    return snapshot;
  }

 public:
  /**
   * 此处已提前构造所有的 Epoch 等元数据,
   * 在 `get_context` 注册流程中, 修改了 idx 后没有更多操作,
   * 不存在 idx 后操作对其他线程尚未可见的问题.
   */
  QSBRManager()
      : ctxs_{std::make_unique<std::array<QSBRContext_, ThreadCnt>>()},
        mgr_idx_{next_mgr_idx.fetch_add(1, std::memory_order_relaxed)} {
    for (ctx_idx_t_ i = 0; i < static_cast<ctx_idx_t_>(ThreadCnt); i++) {
      (*ctxs_)[i].first.store(0, std::memory_order_relaxed);
    }
  }

  /**
   * 如果 `DeleterType_` 的 `operator()` 是非 `const` 的, 则无法使用 `const T&` 的形参, 而是要 `T&` (STL 也如此).
   * 使用转发引用能很好处理 `const` 和左右值.
   */
  template <typename DeleterType_ = DeleterType,
            typename Requires_ = std::enable_if_t<std::is_same_v<std::decay_t<DeleterType_>, DeleterType>>>
  explicit QSBRManager(DeleterType_ &&deleter)
      : DeleterStorage_{std::forward<DeleterType_>(deleter)},
        ctxs_{std::make_unique<std::array<QSBRContext_, ThreadCnt>>()},
        mgr_idx_{next_mgr_idx.fetch_add(1, std::memory_order_relaxed)} {
    for (ctx_idx_t_ i = 0; i < static_cast<ctx_idx_t_>(ThreadCnt); i++) {
      (*ctxs_)[i].first.store(0, std::memory_order_relaxed);
    }
  }

  QSBRManager(const QSBRManager &) = delete;
  auto operator=(const QSBRManager &) -> QSBRManager & = delete;
  QSBRManager(QSBRManager &&) = delete;
  auto operator=(QSBRManager &&) -> QSBRManager & = delete;

  /**
   * 析构 `RetiredContext_` 成员时会 reclaim.
   * `QSBRManager` 的生命周期应该晚于所有线程结束.
   */
  ~QSBRManager() {
    for (ctx_idx_t_ i = 0; i < static_cast<ctx_idx_t_>(ThreadCnt); i++) {
      RetiredContext_ &retired_ctx{(*ctxs_)[i].second};
      retired_ctx.Reclaim(SnapshotFullEpochs(), this->GetDeleter());
    }
    BUSTUB_ASSERT(GetRetiredCntGlobal() == 0, "Detected alive active thread.");
    tls_map.erase(mgr_idx_);
  }

  /**
   * 无论是 `enter` 还是 `exit` 都 Epoch++.
   */
  auto EnterCriticalZone() -> bool {
    auto context{GetContext()};
    if (!context.has_value()) {
      return false;
    }
    QSBRContext_ *ctx{context.value().local_qsbr_ctx_};
    Epoch_ &local_epoch{ctx->first};
    return (local_epoch.fetch_add(1, std::memory_order_acquire) & 1ULL) != 1;
  }

  auto ExitCriticalZone() -> bool {
    auto context{GetContext()};
    if (!context.has_value()) {
      return false;
    }
    QSBRContext_ *ctx{context.value().local_qsbr_ctx_};
    Epoch_ &local_epoch{ctx->first};
    return (local_epoch.fetch_add(1, std::memory_order_release) & 1ULL) != 0;
  }

  auto GetRetiredCntLocal() -> std::uint64_t {
    auto context{GetContext()};
    if (!context.has_value()) {
      return 0;
    }
    QSBRContext_ *ctx{context.value().local_qsbr_ctx_};
    RetiredContext_ &retired_ctx{ctx->second};
    return retired_ctx.GetCnt();
  }

  auto GetRetiredCntGlobal() -> std::uint64_t {
    std::uint64_t cnt_sum{};
    for (ctx_idx_t_ i = 0; i < static_cast<ctx_idx_t_>(ThreadCnt); i++) {
      RetiredContext_ &retired_ctx{(*ctxs_)[i].second};
      cnt_sum += retired_ctx.GetCnt();
    }
    return cnt_sum;
  }

  void Retire(ValType &&val) {
    auto context{GetContext()};
    if (!context.has_value()) {
      return;
    }
    QSBRContext_ *ctx{context.value().local_qsbr_ctx_};
    RetiredContext_ &retired_ctx{ctx->second};
    {
      std::shared_lock<std::shared_mutex> register_lk{register_mtx_};
      retired_ctx.Retire(std::move(val), SnapshotCriticalEpochs());
    }
  }

  void ReclaimLocal() {
    auto context{GetContext()};
    if (!context.has_value()) {
      return;
    }
    QSBRContext_ *ctx{context.value().local_qsbr_ctx_};
    RetiredContext_ &retired_ctx{ctx->second};
    {
      std::shared_lock<std::shared_mutex> register_lk{register_mtx_};
      retired_ctx.Reclaim(SnapshotFullEpochs(), this->GetDeleter());
    }
  }

  /**
   * 设计上 `ReclaimGlobalUnsafe` 不是线程安全的, 不允许并发.
   * 这只允许在所有线程结束后使用.
   */
  void ReclaimGlobalUnsafe() {
    ctx_idx_t_ end_idx{next_ctx_idx_.load(std::memory_order_acquire)};
    for (ctx_idx_t_ i = 0; i < end_idx; i++) {
      RetiredContext_ &retired_ctx{(*ctxs_)[i].second};
      retired_ctx.Reclaim(SnapshotFullEpochs(), this->GetDeleter());
    }
  }
};

/**
 * RAII Guard.
 */
template <std::size_t ThreadCnt, typename ValType, typename DeleterType>
class QSBRGuard {
 private:
  using QSBRManager_ = QSBRManager<ThreadCnt, ValType, DeleterType>;
  QSBRManager_ *mgr_;

 public:
  explicit QSBRGuard(QSBRManager_ &mgr) : mgr_{&mgr} { mgr_->EnterCriticalZone(); }
  QSBRGuard(const QSBRGuard &) = delete;
  auto operator=(const QSBRGuard &) -> QSBRGuard & = delete;

  QSBRGuard(QSBRGuard &&that) noexcept {
    mgr_ = that.mgr_;
    that.mgr_ = nullptr;
  }

  auto operator=(QSBRGuard &&that) noexcept -> QSBRGuard & {
    if (this == &that) {
      return *this;
    }
    if (mgr_) {
      mgr_->ExitCriticalZone();
    }
    mgr_ = that.mgr_;
    that.mgr_ = nullptr;
    return *this;
  }

  ~QSBRGuard() {
    if (mgr_) {
      mgr_->ExitCriticalZone();
      if (mgr_->GetRetiredCntLocal() > 32) {
        mgr_->ReclaimLocal();
      }
    }
  }
};
}  // namespace simple_cu::qsbr

// ============================================================
//
// SmallVector.h
//
// ============================================================

namespace meow_utils {
/**
 * SmallVector for faster stack allocations.
 */
template <typename ValType, std::uint64_t BufferSize, typename Alloc = std::allocator<ValType>>
class SmallVector {
 private:
  alignas(ValType) std::byte inline_data_[sizeof(ValType) * BufferSize];
  ValType *data_pos_{reinterpret_cast<ValType *>(inline_data_)};
  std::uint64_t size_{};
  std::uint64_t capacity_{BufferSize};
  bool is_inline_{true};
  Alloc alloc_;

  static constexpr std::uint64_t INCREMENT_FACTOR{2};
  static constexpr std::uint64_t MAX_SIZE{std::numeric_limits<std::ptrdiff_t>::max() / sizeof(ValType)};

  using allocator_traits_ = std::allocator_traits<Alloc>;

  static constexpr bool VAL_COPY_MOVE_CTOR_NOEXCEPT{std::is_nothrow_copy_constructible_v<ValType> ||
                                                    std::is_nothrow_move_constructible_v<ValType>};
  static constexpr bool VAL_COPY_MOVE_ASSIGN_NOEXCEPT{std::is_nothrow_copy_assignable_v<ValType> ||
                                                      std::is_nothrow_move_assignable_v<ValType>};
  static constexpr bool VAL_DTOR_NOEXCEPT{std::is_nothrow_destructible_v<ValType>};

  static constexpr bool ALLOC_COPY_CTOR_NOEXCEPT{std::is_nothrow_copy_constructible_v<Alloc>};
  static constexpr bool ALLOC_MOVE_CTOR_NOEXCEPT{std::is_nothrow_move_constructible_v<Alloc>};
  static constexpr bool ALLOC_COPY_ASSIGN_NOEXCEPT{std::is_nothrow_copy_assignable_v<Alloc>};
  static constexpr bool ALLOC_MOVE_ASSIGN_NOEXCEPT{std::is_nothrow_move_assignable_v<Alloc>};

  static constexpr bool ALLOC_SELECT_NOEXCEPT{
      noexcept(allocator_traits_::select_on_container_copy_construction(std::declval<Alloc>()))};

  template <bool IsConst>
  class IteratorImpl {
   private:
    std::conditional_t<IsConst, const ValType *, ValType *> ptr_;

   public:
    using difference_type = std::ptrdiff_t;
    using value_type = ValType;
    using pointer = std::conditional_t<IsConst, const ValType *, ValType *>;
    using reference = std::conditional_t<IsConst, const ValType &, ValType &>;
    using iterator_category = std::random_access_iterator_tag;

    IteratorImpl() noexcept = default;
    // No `explicit`.
    IteratorImpl(const pointer &ptr) noexcept : ptr_{ptr} {  // NOLINT
    }

    IteratorImpl(const IteratorImpl &) noexcept = default;
    IteratorImpl(IteratorImpl &&) noexcept = default;
    auto operator=(const IteratorImpl &) noexcept -> IteratorImpl & = default;
    auto operator=(IteratorImpl &&) noexcept -> IteratorImpl & = default;
    ~IteratorImpl() noexcept = default;

    template <bool IsConst_>
    friend class IteratorImpl;

    // No `explicit`.
    template <bool IsConst_, typename Requires_ = std::enable_if_t<
                                 !IsConst_ && !std::is_base_of_v<IteratorImpl, IteratorImpl<IsConst_>>>>
    IteratorImpl(const IteratorImpl<IsConst_> &that) noexcept : ptr_{that.ptr_} {  // NOLINT
    }

    auto operator*() const noexcept -> const ValType & { return *ptr_; }

    auto operator*() noexcept -> ValType & { return *ptr_; }

    auto operator->() const noexcept -> const ValType * { return ptr_; }

    auto operator->() noexcept -> ValType * { return ptr_; }

    auto operator++() noexcept -> IteratorImpl & {
      ++ptr_;
      return *this;
    }
    auto operator++(int) noexcept -> IteratorImpl {
      IteratorImpl tmp{*this};
      ++(*this);
      return tmp;
    }
    auto operator--() noexcept -> IteratorImpl & {
      --ptr_;
      return *this;
    }
    auto operator--(int) noexcept -> IteratorImpl {
      IteratorImpl tmp{*this};
      --(*this);
      return tmp;
    }
    auto operator+=(difference_type n) noexcept -> IteratorImpl & {
      ptr_ += n;
      return *this;
    }

    auto operator-=(difference_type n) noexcept -> IteratorImpl & {
      ptr_ -= n;
      return *this;
    }

    auto operator[](std::ptrdiff_t n) const noexcept -> const ValType & { return *(*this + n); }

    auto operator[](std::ptrdiff_t n) noexcept -> ValType & { return *(*this + n); }

    auto operator+(std::ptrdiff_t n) const noexcept -> IteratorImpl {
      IteratorImpl tmp{*this};
      tmp += n;
      return tmp;
    }

    auto operator-(std::ptrdiff_t n) const noexcept -> IteratorImpl {
      IteratorImpl tmp{*this};
      tmp -= n;
      return tmp;
    }

    template <bool IsConstA, bool IsConstB>
    friend auto operator==(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;
    template <bool IsConstA, bool IsConstB>
    friend auto operator!=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;
    template <bool IsConstA, bool IsConstB>
    friend auto operator<(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;
    template <bool IsConstA, bool IsConstB>
    friend auto operator>(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;
    template <bool IsConstA, bool IsConstB>
    friend auto operator<=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;
    template <bool IsConstA, bool IsConstB>
    friend auto operator>=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool;

    template <bool IsConstA, bool IsConstB>
    friend auto operator-(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> std::ptrdiff_t;
  };

  template <bool IsConstA, bool IsConstB>
  friend auto operator==(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ == b.ptr_;
  }
  template <bool IsConstA, bool IsConstB>
  friend auto operator!=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ != b.ptr_;
  }

  template <bool IsConstA, bool IsConstB>
  friend auto operator<(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ < b.ptr_;
  }

  template <bool IsConstA, bool IsConstB>
  friend auto operator>(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ > b.ptr_;
  }

  template <bool IsConstA, bool IsConstB>
  friend auto operator<=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ <= b.ptr_;
  }
  template <bool IsConstA, bool IsConstB>
  friend auto operator>=(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> bool {
    return a.ptr_ >= b.ptr_;
  }

  template <bool IsConstA, bool IsConstB>
  friend auto operator-(const IteratorImpl<IsConstA> &a, const IteratorImpl<IsConstB> &b) -> std::ptrdiff_t {
    return a.ptr_ - b.ptr_;
  }

 private:
  auto AllocHeap(std::uint64_t capacity) -> ValType * { return allocator_traits_::allocate(alloc_, capacity); }

  void DeallocHeap(ValType *heap_data, std::uint64_t capacity) {
    allocator_traits_::deallocate(alloc_, heap_data, capacity);
  }

  void DestructIn(ValType *pos, std::uint64_t size) noexcept(VAL_DTOR_NOEXCEPT) {
    if constexpr (!std::is_trivially_destructible_v<ValType>) {
      std::ptrdiff_t end{static_cast<std::ptrdiff_t>(size)};
      for (std::ptrdiff_t i = 0; i < end; i++) {
        allocator_traits_::destroy(alloc_, pos++);
      }
    }
  }

  void UninitCopyElements(ValType *dst, const ValType *src, std::uint64_t size) noexcept(VAL_COPY_MOVE_CTOR_NOEXCEPT) {
    if constexpr (std::is_trivially_copyable_v<ValType>) {
      std::memcpy(dst, src, sizeof(ValType) * size);
    } else {
      std::ptrdiff_t end{static_cast<std::ptrdiff_t>(size)};
      if constexpr (std::is_nothrow_copy_constructible_v<ValType>) {
        for (std::ptrdiff_t i = 0; i < end; i++) {
          allocator_traits_::construct(alloc_, dst++, src[i]);
        }
      } else {
        std::ptrdiff_t i{};
        ValType *tmp{AllocHeap(size)};
        ValType *tmp_cur{};
        try {
          for (tmp_cur = tmp, i = 0; i < end; tmp_cur++, i++) {
            allocator_traits_::construct(alloc_, tmp_cur, src[i]);
          }
          for (tmp_cur = tmp, i = 0; i < end; tmp_cur++, i++, dst++) {
            allocator_traits_::construct(alloc_, dst, std::move(*tmp_cur));
          }
          DestructIn(tmp, size);
          DeallocHeap(tmp, size);
        } catch (...) {
          DestructIn(tmp, i);
          DeallocHeap(tmp, size);
          throw;
        }
      }
    }
  }

  void CopyElements(ValType *dst, const ValType *src, std::uint64_t size) noexcept(VAL_COPY_MOVE_ASSIGN_NOEXCEPT) {
    if constexpr (std::is_trivially_copyable_v<ValType>) {
      std::memcpy(dst, src, sizeof(ValType) * size);
    } else {
      std::ptrdiff_t end{static_cast<std::ptrdiff_t>(size)};
      if constexpr (std::is_nothrow_copy_assignable_v<ValType>) {
        for (std::ptrdiff_t i = 0; i < end; i++) {
          *dst++ = src[i];
        }
      } else {
        std::ptrdiff_t i{};
        ValType *tmp{AllocHeap(size)};
        ValType *tmp_cur{};
        try {
          for (tmp_cur = tmp, i = 0; i < end; tmp_cur++, i++) {
            allocator_traits_::construct(alloc_, tmp_cur, src[i]);
          }
          for (tmp_cur = tmp, i = 0; i < end; tmp_cur++, i++, dst++) {
            *dst = std::move(*tmp_cur);
          }
          DestructIn(tmp, size);
          DeallocHeap(tmp, size);
        } catch (...) {
          DestructIn(tmp, i);
          DeallocHeap(tmp, size);
          throw;
        }
      }
    }
  }

  void UninitMoveElements(ValType *dst, ValType *src, std::uint64_t size) noexcept(VAL_COPY_MOVE_CTOR_NOEXCEPT) {
    if constexpr (std::is_trivially_copyable_v<ValType>) {
      std::memmove(dst, src, sizeof(ValType) * size);
    } else {
      if constexpr (std::is_nothrow_move_constructible_v<ValType>) {
        for (std::uint64_t i = 0; i < size; i++) {
          allocator_traits_::construct(alloc_, dst++, std::move(src[i]));
        }
      } else {
        for (std::uint64_t i = 0; i < size; i++) {
          allocator_traits_::construct(alloc_, dst++, src[i]);
        }
      }
    }
  }

  void MoveElements(ValType *dst, ValType *src, std::uint64_t size) noexcept(VAL_COPY_MOVE_ASSIGN_NOEXCEPT) {
    if constexpr (std::is_trivially_copyable_v<ValType>) {
      std::memmove(dst, src, sizeof(ValType) * size);
    } else {
      if constexpr (std::is_nothrow_move_assignable_v<ValType>) {
        for (std::uint64_t i = 0; i < size; i++) {
          *dst++ = std::move(src[i]);
        }
      } else {
        for (std::uint64_t i = 0; i < size; i++) {
          *dst++ = src[i];
        }
      }
    }
  }

  void ExpandWith(std::ptrdiff_t offset, std::uint64_t with_size, std::uint64_t incre_factor = INCREMENT_FACTOR) {
    std::uint64_t needed_capacity{size_ + with_size};
    if (needed_capacity > MAX_SIZE) {
      throw std::length_error{"Exceed max size."};
    }
    std::ptrdiff_t end{static_cast<std::ptrdiff_t>(size_)};
    if (needed_capacity <= capacity_) [[likely]] {
      if (offset == end) [[likely]] {  // Fast path for `emplace_back`.
        return;
      } else {
        bool is_overlapped{with_size < size_ - offset};
        if (!is_overlapped) [[likely]] {
          UninitMoveElements(data_pos_ + offset + with_size, data_pos_ + offset, size_ - offset);
        } else {
          UninitMoveElements(data_pos_ + size_, data_pos_ + size_ - with_size, with_size);
          MoveElements(data_pos_ + offset + with_size, data_pos_ + offset, size_ - offset - with_size);
        }
        DestructIn(data_pos_ + offset, with_size);
      }
    } else {
      std::uint64_t new_capacity{std::max(size_ * incre_factor, needed_capacity)};
      ValType *new_heap_data{AllocHeap(new_capacity)};
      UninitMoveElements(new_heap_data, data_pos_, offset);
      UninitMoveElements(new_heap_data + offset + with_size, data_pos_ + offset, size_ - offset);
      DestructIn(data_pos_, size_);
      if (!is_inline_) {
        DeallocHeap(data_pos_, capacity_);
      }
      is_inline_ = false;
      capacity_ = new_capacity;
      data_pos_ = new_heap_data;
    }
  }

  void ShrinkWithout(std::ptrdiff_t offset, std::uint64_t without_size) {
    std::uint64_t needed_capacity{size_ - without_size};
    ValType *inline_data{reinterpret_cast<ValType *>(inline_data_)};
    if (is_inline_) [[likely]] {
      MoveElements(inline_data + offset, inline_data + offset + without_size, size_ - (offset + without_size));
      DestructIn(inline_data + size_ - without_size, without_size);
    } else {
      if (needed_capacity > BufferSize) [[likely]] {
        MoveElements(data_pos_ + offset, data_pos_ + offset + without_size, size_ - (offset + without_size));
        DestructIn(data_pos_ + size_ - without_size, without_size);
      } else {
        UninitMoveElements(inline_data, data_pos_, offset);
        UninitMoveElements(inline_data + offset, data_pos_ + offset + without_size, size_ - (offset + without_size));
        DestructIn(data_pos_, size_);
        DeallocHeap(data_pos_, capacity_);
        is_inline_ = true;
        capacity_ = BufferSize;
        data_pos_ = inline_data;
      }
    }
  }

 public:
  using value_type = ValType;
  using reference = ValType &;
  using iterator = IteratorImpl<false>;
  using const_iterator = IteratorImpl<true>;
  using difference_type = std::ptrdiff_t;
  using size_type = std::size_t;

  using Iterator = IteratorImpl<false>;
  using ConstIterator = IteratorImpl<true>;
  using ReverseIterator = std::reverse_iterator<Iterator>;
  using ConstReverseIterator = std::reverse_iterator<ConstIterator>;

  SmallVector() = default;

  SmallVector(const SmallVector &that) {
    auto alloc_select{allocator_traits_::select_on_container_copy_construction(that.alloc_)};
    ::new (std::addressof(alloc_)) Alloc{std::move(alloc_select)};
    if (!that.is_inline_) [[unlikely]] {
      data_pos_ = AllocHeap(that.capacity_);
    }
    UninitCopyElements(data_pos_, that.data_pos_, that.size_);
    size_ = that.size_;
    capacity_ = that.capacity_;
    is_inline_ = that.is_inline_;
  }

  SmallVector(SmallVector &&that) noexcept {
    ::new (std::addressof(alloc_)) Alloc{std::move(that.alloc_)};
    if (that.is_inline_) [[likely]] {
      ValType *inline_data{reinterpret_cast<ValType *>(inline_data_)};
      ValType *that_inline_data{reinterpret_cast<ValType *>(that.inline_data_)};
      UninitMoveElements(inline_data, that_inline_data, that.size_);
    } else {
      data_pos_ = that.data_pos_;
      that.data_pos_ = nullptr;
    }
    size_ = that.size_;
    capacity_ = that.capacity_;
    is_inline_ = that.is_inline_;
  }

  auto operator=(const SmallVector &that) -> SmallVector & {
    if (this == std::addressof(that)) {
      return *this;
    }
    DestructIn(data_pos_, size_);
    size_ = 0;

    if constexpr (allocator_traits_::propagate_on_container_copy_assignment::value) {
      if constexpr (!allocator_traits_::is_always_equal::value) {
        if (alloc_ != that.alloc_) {
          if (!is_inline_) [[unlikely]] {
            DeallocHeap(data_pos_, capacity_);
            ValType *inline_data{reinterpret_cast<ValType *>(inline_data_)};
            data_pos_ = inline_data;
            capacity_ = BufferSize;
            is_inline_ = true;
          }
          alloc_ = that.alloc_;
        }
      }
    }

    if (that.size_ > capacity_) [[unlikely]] {
      data_pos_ = AllocHeap(that.capacity_);
      capacity_ = that.capacity_;
      is_inline_ = false;
    }
    UninitCopyElements(data_pos_, that.data_pos_, that.size_);
    size_ = that.size_;
    return *this;
  }

  auto operator=(SmallVector &&that) noexcept -> SmallVector & {
    if (this == std::addressof(that)) {
      return *this;
    }
    DestructIn(data_pos_, size_);
    if constexpr (allocator_traits_::propagate_on_container_move_assignment::value) {
      if constexpr (!allocator_traits_::is_always_equal::value) {
        if (alloc_ != that.alloc_) {
          if (!is_inline_) [[unlikely]] {
            DeallocHeap(data_pos_, capacity_);
            ValType *inline_data{reinterpret_cast<ValType *>(inline_data_)};
            data_pos_ = inline_data;
            capacity_ = BufferSize;
            is_inline_ = true;
          }
          alloc_ = std::move(that.alloc_);
        }
      }
    }

    if (that.is_inline_) [[likely]] {
      UninitMoveElements(data_pos_, that.data_pos_, that.size_);
    } else {
      data_pos_ = that.data_pos_;
      that.data_pos_ = nullptr;
      capacity_ = that.capacity_;
      is_inline_ = false;
    }
    size_ = that.size_;
    return *this;
  }

  ~SmallVector() noexcept(VAL_DTOR_NOEXCEPT) {
    if (data_pos_) [[likely]] {
      DestructIn(data_pos_, size_);
      if (!is_inline_) [[unlikely]] {
        DeallocHeap(data_pos_, capacity_);
      }
    }
  }

  auto operator[](std::ptrdiff_t index) const noexcept -> const ValType & {
    return *std::launder(std::addressof(data_pos_[index]));
  }

  auto operator[](std::ptrdiff_t index) noexcept -> ValType & {
    return *std::launder(std::addressof(data_pos_[index]));
  }

  // NOLINTNEXTLINE
  auto begin() const noexcept -> ConstIterator { return std::launder(std::addressof(data_pos_[0])); }

  // NOLINTNEXTLINE
  auto end() const noexcept -> ConstIterator { return data_pos_ + size_; }
  // NOLINTNEXTLINE
  auto begin() noexcept -> Iterator { return std::launder(std::addressof(data_pos_[0])); }
  // NOLINTNEXTLINE
  auto end() noexcept -> Iterator { return data_pos_ + size_; }
  // NOLINTNEXTLINE
  auto cbegin() const noexcept -> ConstIterator { return std::launder(std::addressof(data_pos_[0])); }
  // NOLINTNEXTLINE
  auto cend() const noexcept -> ConstIterator { return data_pos_ + size_; }
  // NOLINTNEXTLINE
  auto rbegin() const noexcept -> ConstReverseIterator { return ConstReverseIterator{end()}; }
  // NOLINTNEXTLINE
  auto rend() const noexcept -> ConstReverseIterator { return ConstReverseIterator{begin()}; }
  // NOLINTNEXTLINE
  auto rbegin() noexcept -> ReverseIterator { return ReverseIterator{end()}; }
  // NOLINTNEXTLINE
  auto rend() noexcept -> ReverseIterator { return ReverseIterator{begin()}; }
  // NOLINTNEXTLINE
  auto crbegin() const noexcept -> ConstReverseIterator { return ConstReverseIterator{cend()}; }
  // NOLINTNEXTLINE
  auto crend() const noexcept -> ConstReverseIterator { return ConstReverseIterator{cbegin()}; }

  auto Size() const noexcept -> std::uint64_t { return size_; }

  auto Empty() const noexcept -> bool { return size_ == 0; }

  auto MaxSize() const noexcept -> std::uint64_t {
    return std::numeric_limits<std::ptrdiff_t>::max() / sizeof(ValType);
  }

  auto Capacity() const noexcept -> std::uint64_t { return capacity_; }

  auto Back() const noexcept -> const ValType & { return *std::launder(std::addressof(data_pos_[size_ - 1])); }

  auto Back() noexcept -> ValType & { return *std::launder(std::addressof(data_pos_[size_ - 1])); }

  auto Front() const noexcept -> const ValType & { return *std::launder(std::addressof(data_pos_[0])); }

  auto Front() noexcept -> ValType & { return *std::launder(std::addressof(data_pos_[0])); }

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_>>>
  auto Insert(ConstIterator pos, ValType_ &&val) -> Iterator {
    std::ptrdiff_t offset{pos - cbegin()};
    ExpandWith(offset, 1, INCREMENT_FACTOR);
    allocator_traits_::construct(alloc_, std::addressof(data_pos_[offset]), std::forward<ValType_>(val));
    ++size_;
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  auto Insert(ConstIterator pos, ConstIterator beg, ConstIterator end) -> Iterator {
    std::ptrdiff_t offset{pos - cbegin()};
    std::ptrdiff_t len{end - beg};
    ExpandWith(offset, len, INCREMENT_FACTOR);
    UninitCopyElements(std::addressof(data_pos_[offset]), std::addressof(*beg), len);
    size_ += len;
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  template <typename InputIter_,
            typename Requires_ = std::enable_if_t<!std::is_constructible_v<ConstIterator, InputIter_>>>
  auto Insert(ConstIterator pos, InputIter_ beg, InputIter_ end) -> Iterator {
    using iter_categroy = typename std::iterator_traits<InputIter_>::iterator_category;
    std::ptrdiff_t offset{pos - cbegin()};
    if constexpr (std::is_base_of_v<std::random_access_iterator_tag, iter_categroy>) {
      std::ptrdiff_t len{end - beg};
      ExpandWith(offset, len, INCREMENT_FACTOR);
      for (std::ptrdiff_t i = offset; beg != end; i++, beg++) {
        allocator_traits_::construct(alloc_, std::addressof(data_pos_[i]), *beg);
      }
      size_ += len;
    } else {
      SmallVector<ValType, BufferSize> tmp{};
      for (; beg != end; beg++) {
        tmp.EmplaceBack(*beg);
      }
      this->Insert(pos, tmp.begin(), tmp.end());
    }
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_>>>
  auto Insert(ConstIterator pos, std::initializer_list<ValType_> list) -> Iterator {
    std::ptrdiff_t offset{pos - cbegin()};
    ExpandWith(offset, list.size(), INCREMENT_FACTOR);
    UninitCopyElements(std::addressof(data_pos_[offset]), list.begin(), list.size());
    size_ += list.size();
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  auto Erase(ConstIterator pos) -> Iterator {
    std::ptrdiff_t offset{pos - cbegin()};
    ShrinkWithout(offset, 1);
    --size_;
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  auto Erase(ConstIterator beg, ConstIterator end) -> Iterator {
    std::ptrdiff_t offset{beg - cbegin()};
    std::ptrdiff_t len{end - beg};
    ShrinkWithout(offset, len);
    size_ -= len;
    return Iterator{std::launder(std::addressof(data_pos_[offset]))};
  }

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_>>>
  void PushBack(ValType_ &&val) {
    ExpandWith(size_, 1, INCREMENT_FACTOR);
    allocator_traits_::construct(alloc_, std::addressof(data_pos_[size_]), std::forward<ValType_>(val));
    ++size_;
  }

  template <typename... Args, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, Args...>>>
  auto EmplaceBack(Args &&...args) -> ValType & {
    ExpandWith(size_, 1, INCREMENT_FACTOR);
    allocator_traits_::construct(alloc_, std::addressof(data_pos_[size_]), std::forward<Args>(args)...);
    return *std::launder(std::addressof(data_pos_[size_++]));
  }

  void PopBack() {
    ShrinkWithout(size_ - 1, 1);
    allocator_traits_::destroy(alloc_, std::addressof(data_pos_[size_]));
    --size_;
  }

  void Resize(std::uint64_t size) {
    if (size >= size_) [[likely]] {
      ExpandWith(size_, size - size_);
    } else {
      ShrinkWithout(size_, size_ - size);
    }
    std::ptrdiff_t end{static_cast<std::ptrdiff_t>(size)};
    for (std::ptrdiff_t i = size_; i < end; i++) {
      allocator_traits_::construct(alloc_, std::addressof(data_pos_[i]), ValType{});
    }
    size_ = size;
  }

  void Reserve(std::uint64_t capacity) {
    if (capacity < capacity_) {
      return;
    }
    ExpandWith(size_, capacity - size_, 1);
    capacity_ = capacity;
  }

  void Clear() noexcept {
    ShrinkWithout(0, size_);
    size_ = 0;
  }
};
}  // namespace meow_utils

namespace bustub {

struct FastHash {
  auto operator()(std::uint64_t id, std::uint64_t salt) const -> std::uint64_t {
    constexpr std::uint64_t magic1{0x9E3779B97F4A7C15ULL};
    constexpr std::uint64_t magic2{0xBF58476D1CE4E5B9ULL};

    std::uint64_t x{id + (salt * magic1)};
    x = (x ^ (x >> 30)) * magic2;
    x = (x ^ (x >> 27)) * magic1;
    x ^= (x >> 31);
    return x;
  }
};

/**
 * @brief Represents a Write or Read request for the DiskManager to execute.
 *
 * 读写请求
 */
struct DiskRequest {
  /** Flag indicating whether the request is a write or a read. */
  bool is_flush_{};

  /**
   *  Pointer to the start of the memory location where a page is either:
   *   1. being read into from disk (on a read).
   *   2. being written out to disk (on a write).
   */
  char *data_{};  // memory 数据

  /** ID of the page being read from / written to disk. */
  page_id_t page_id_{INVALID_PAGE_ID};  // 目标 disk page

  /** Callback used to signal to the request issuer when the request has been completed. */
  std::promise<bool> callback_;

  bool async_hint_{};
};

/**
 * @brief A simple page allocator.
 *
 * This is only used in DiskScheduler temporary flush buffer.
 *
 * 这和 `BufferPoolManager` 的职责不同,
 * `ConcurrentTempFrameBuffer` 仅分配临时使用的 frame,
 * 不需要维护 id 映射.
 */
class ConcurrentTempFrameBuffer {
 private:
  /**
   * 使用 Queue 来避免 Stack 在 top 和 top frame 的 cache pingpong.
   */

  simple_cu::utils::SimpleConcurrentQueue<char *> available_frames_;
  size_t base_frame_cnt_;
  char *base_;

 public:
  explicit ConcurrentTempFrameBuffer(size_t base_frame_cnt);
  ConcurrentTempFrameBuffer(const ConcurrentTempFrameBuffer &) = delete;
  auto operator=(const ConcurrentTempFrameBuffer &) -> ConcurrentTempFrameBuffer & = delete;
  ConcurrentTempFrameBuffer(ConcurrentTempFrameBuffer &) = delete;
  auto operator=(ConcurrentTempFrameBuffer &&) -> ConcurrentTempFrameBuffer & = delete;
  ~ConcurrentTempFrameBuffer();

  /**
   * 不保证成功.
   * 若失败则 Flush 退化为同步操作.
   */
  auto Allocate() -> char *;
  auto TryAllocate(char *&res) -> bool;
  void Deallocate(char *ptr);
  auto IsDeallocatable(const char *ptr) -> bool;
  auto TryDeallocate(char *ptr) -> bool;
};

/**
 * @brief The DiskScheduler schedules disk read and write operations.
 *
 * A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
 * maintains a background worker thread that processes the scheduled requests using the disk manager. The background
 * thread is created in the DiskScheduler constructor and joined in its destructor.
 */
class DiskScheduler {
 public:
  // DiskScheduler 是 DiskManager 的 wrapper.
  explicit DiskScheduler(DiskManager *disk_manager);
  ~DiskScheduler();

  // 通过 Schedule 启动一个写磁盘任务.
  auto Schedule(DiskRequest r) -> bool;

  void StartWorkerThread(size_t shard_id);

  using DiskSchedulerPromise = std::promise<bool>;

  /**
   * @brief Create a Promise object. If you want to implement your own version of promise, you can change this function
   * so that our test cases can use your promise implementation.
   *
   * @return std::promise<bool>
   *
   * 这里 bustub 使用 promise/future 仅用于异步等待,
   *  future 没有返回信息.
   */
  auto CreatePromise() -> DiskSchedulerPromise { return DiskSchedulerPromise{}; };

  /**
   * @brief Deallocates a page on disk.
   *
   * Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
   *
   * @param page_id The page ID of the page to deallocate from disk.
   */
  void DeallocatePage(page_id_t page_id) { disk_manager_->DeletePage(page_id); }

 private:
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));

  constexpr static std::size_t SHARD_FACTOR{2};
  constexpr static std::size_t TEMPFRAME_CNT_PERSHARD{8};

  struct PageTaskBlock;

  /**
   * PTB 表.
   */
  struct PageTaskTable {
    /**
     * Shard 存 ptr 避免扩容改变地址.
     */
    struct alignas(simple_cu::utils::CACHELINE_SIZE) PageTaskTableShard {
      std::mutex m_;
      std::unordered_map<page_id_t, std::unique_ptr<PageTaskBlock>> table_;
    };

    std::vector<PageTaskTableShard> shards_;

    PageTaskTable();
    auto VisitShard(page_id_t page_id) -> PageTaskTableShard *;
  };

  /**
   * Worker 表.
   */
  struct WorkerTable {
    /**
     * `pending_queue_` 应该改成 bucket 电梯调度.
     */
    struct alignas(simple_cu::utils::CACHELINE_SIZE) WorkerShard {
      simple_cu::utils::SimpleConcurrentQueue<PageTaskBlock *> pending_queue_;
      ConcurrentTempFrameBuffer temp_frame_buffer_{TEMPFRAME_CNT_PERSHARD};
    };

    std::vector<WorkerShard> shards_;

    WorkerTable();
    auto VisitShard(page_id_t page_id) -> WorkerShard *;
  };

  /**
   * 维护对每个 `page_id_` 的 Onflight Fetch/Flush 请求.
   * 每个 `WorkerShard` 都 own 部分 `PageTaskBlock`.
   */
  struct alignas(simple_cu::utils::CACHELINE_SIZE) PageTaskBlock {
    struct Callback {
      std::optional<DiskSchedulerPromise> callback_opt_;
      char *validation_ptr_;
    };

    /**
     * `PENDING_*` 和 `IS_INQUEUE` 必须是原子的,
     * `PENDING_*` 的变化必须阻止 unset `IS_INQUEUE`.
     */
    enum State : std::uint8_t {
      EMPTY = 0,
      PENDING_FLUSH = 1ULL << 0,
      PENDING_FETCH = 1ULL << 1,
      IS_INQUEUE = 1ULL << 2,
    };

    /**
     * Producer 先存 callback, 再存 pointer.
     * Consumer 先取 pointer, 再取 callback.
     *
     * 同一时间, 必须保证只有一个 Producer 能发起对一个 page 的 flush/fetch.
     * 一个 page 同一时间只能由一个 worker 处理任务.
     */

    /**
     * 考虑连续对同一个 ptr 发起 Flush 的情况.
     * 首先, 这不可能是 async 请求, 因为 ptr 尚未被回收, 无法重用.
     * 如果是同步, 在上一个 Flush 还未完成的情况下, 不可能再次 Flush.
     * 则, `pending_*_cb_` 中不可能存在相同 `validation_ptr_` 的 Callback.
     *
     * > 由上,
     * > Worker 从 `pending_*_cb_` 取出和当前 ptr 匹配的 Callback 即可直接返回.
     */

    std::atomic<char *> flush_src_{};  // Tagged Pointer.
    std::atomic<char *> fetch_dst_{};
    moodycamel::ReaderWriterQueue<Callback> pending_flush_cb_;
    moodycamel::ReaderWriterQueue<Callback> pending_fetch_cb_;

    std::atomic<std::underlying_type_t<State>> state_{};

    /**
     * Shared 字段.
     */
    alignas(simple_cu::utils::CACHELINE_SIZE) page_id_t const ptb_page_id_{INVALID_PAGE_ID};
    WorkerTable::WorkerShard *const to_worker_shard_;

    PageTaskBlock(page_id_t page_id, WorkerTable::WorkerShard *to_worker_shard);

    void SubmitFlushTask(char *new_flush_src, bool is_deallocable, DiskSchedulerPromise &&callback);
    void SubmitFetchTask(char *new_fetch_dst, bool is_deallocable, DiskSchedulerPromise &&callback);
    auto GatherFlushTask(std::vector<DiskSchedulerPromise> &flush_cb) -> char *;
    auto GatherFetchTask(std::vector<DiskSchedulerPromise> &fetch_cb) -> char *;
    auto TrySetDequeued() -> bool;
  };

  WorkerTable worker_table_;
  PageTaskTable global_ptb_table_;
  std::vector<std::thread> worker_;

  std::atomic<bool> stop_{};
};
}  // namespace bustub
