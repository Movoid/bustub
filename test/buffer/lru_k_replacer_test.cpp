//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer_test.cpp
//
// Identification: test/buffer/lru_k_replacer_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * lru_k_replacer_test.cpp
 */

#include "buffer/lru_k_replacer.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <set>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"

namespace bustub {

TEST(LRUKReplacerTest, SampleTest) {
  // Note that comparison with `std::nullopt` always results in `false`, and if the optional type actually does contain
  // a value, the comparison will compare the inner value.
  // See: https://devblogs.microsoft.com/oldnewthing/20211004-00/?p=105754
  std::optional<frame_id_t> frame;

  // Initialize the replacer.
  LRUKReplacer lru_replacer(7, 2);

  // Add six frames to the replacer. We now have frames [1, 2, 3, 4, 5]. We set frame 6 as non-evictable.
  lru_replacer.RecordAccess(1);
  lru_replacer.RecordAccess(2);
  lru_replacer.RecordAccess(3);
  lru_replacer.RecordAccess(4);
  lru_replacer.RecordAccess(5);
  lru_replacer.RecordAccess(6);
  lru_replacer.SetEvictable(1, true);
  lru_replacer.SetEvictable(2, true);
  lru_replacer.SetEvictable(3, true);
  lru_replacer.SetEvictable(4, true);
  lru_replacer.SetEvictable(5, true);
  lru_replacer.SetEvictable(6, false);

  // The size of the replacer is the number of frames that can be evicted, _not_ the total number of frames entered.
  ASSERT_EQ(5, lru_replacer.Size());

  // Record an access for frame 1. Now frame 1 has two accesses total.
  lru_replacer.RecordAccess(1);
  // All other frames now share the maximum backward k-distance. Since we use timestamps to break ties, where the first
  // to be evicted is the frame with the oldest timestamp, the order of eviction should be [2, 3, 4, 5, 1].

  // Evict three pages from the replacer.
  // To break ties, we use LRU with respect to the oldest timestamp, or the least recently used frame.
  ASSERT_EQ(2, lru_replacer.Evict());
  ASSERT_EQ(3, lru_replacer.Evict());
  ASSERT_EQ(4, lru_replacer.Evict());
  ASSERT_EQ(2, lru_replacer.Size());
  // Now the replacer has the frames [5, 1].

  // Insert new frames [3, 4], and update the access history for 5. Now, the ordering is [3, 1, 5, 4].
  lru_replacer.RecordAccess(3);
  lru_replacer.RecordAccess(4);
  lru_replacer.RecordAccess(5);
  lru_replacer.RecordAccess(4);
  lru_replacer.SetEvictable(3, true);
  lru_replacer.SetEvictable(4, true);
  ASSERT_EQ(4, lru_replacer.Size());

  // Look for a frame to evict. We expect frame 3 to be evicted next.
  ASSERT_EQ(3, lru_replacer.Evict());
  ASSERT_EQ(3, lru_replacer.Size());

  // Set 6 to be evictable. 6 Should be evicted next since it has the maximum backward k-distance.
  lru_replacer.SetEvictable(6, true);
  ASSERT_EQ(4, lru_replacer.Size());
  ASSERT_EQ(6, lru_replacer.Evict());
  ASSERT_EQ(3, lru_replacer.Size());

  // Mark frame 1 as non-evictable. We now have [5, 4].
  lru_replacer.SetEvictable(1, false);

  // We expect frame 5 to be evicted next.
  ASSERT_EQ(2, lru_replacer.Size());
  ASSERT_EQ(5, lru_replacer.Evict());
  ASSERT_EQ(1, lru_replacer.Size());

  // Update the access history for frame 1 and make it evictable. Now we have [4, 1].
  lru_replacer.RecordAccess(1);
  lru_replacer.RecordAccess(1);
  lru_replacer.SetEvictable(1, true);
  ASSERT_EQ(2, lru_replacer.Size());

  // Evict the last two frames.
  ASSERT_EQ(4, lru_replacer.Evict());
  ASSERT_EQ(1, lru_replacer.Size());
  ASSERT_EQ(1, lru_replacer.Evict());
  ASSERT_EQ(0, lru_replacer.Size());

  // Insert frame 1 again and mark it as non-evictable.
  lru_replacer.RecordAccess(1);
  lru_replacer.SetEvictable(1, false);
  ASSERT_EQ(0, lru_replacer.Size());

  // A failed eviction should not change the size of the replacer.
  frame = lru_replacer.Evict();
  ASSERT_EQ(false, frame.has_value());

  // Mark frame 1 as evictable again and evict it.
  lru_replacer.SetEvictable(1, true);
  ASSERT_EQ(1, lru_replacer.Size());
  ASSERT_EQ(1, lru_replacer.Evict());
  ASSERT_EQ(0, lru_replacer.Size());

  // There is nothing left in the replacer, so make sure this doesn't do something strange.
  frame = lru_replacer.Evict();
  ASSERT_EQ(false, frame.has_value());
  ASSERT_EQ(0, lru_replacer.Size());

  // Make sure that setting a non-existent frame as evictable or non-evictable doesn't do something strange.
  lru_replacer.SetEvictable(6, false);
  lru_replacer.SetEvictable(6, true);
}

TEST(LRUKReplacerTest, My_EvictTest) {
  LRUKReplacer replacer{500, 5};

  for (int i = 0; i < 500; i++) {
    replacer.RecordAccess(i);
    replacer.SetEvictable(i, true);
  }

  ASSERT_EQ(500, replacer.Size());

  int tmp{500};
  for (int i = 0; i < 250; i++) {
    replacer.Remove(i);
    tmp--;
    ASSERT_EQ(tmp, replacer.Size());
  }

  for (int i = 250; i < 260; i++) {
    replacer.RecordAccess(i);
  }
  for (int i = 250; i < 260; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(240, replacer.Size());

  for (int j = 0; j < 4; j++) {
    for (int i = 260; i < 270; i++) {
      replacer.RecordAccess(i);
    }
  }
  for (int i = 270; i < 280; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(230, replacer.Size());

  for (int i = 280; i < 290; i++) {
    replacer.SetEvictable(i, false);
  }
  ASSERT_EQ(220, replacer.Size());
  for (int i = 290; i < 300; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(210, replacer.Size());

  for (int i = 280; i < 290; i++) {
    replacer.SetEvictable(i, true);
  }
  ASSERT_EQ(220, replacer.Size());
  for (int i = 280; i < 290; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(210, replacer.Size());

  for (int i = 300; i < 500; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(10, replacer.Size());
  for (int i = 260; i < 270; i++) {
    auto evicted{replacer.Evict()};
    ASSERT_EQ(true, evicted.has_value());
    ASSERT_EQ(i, evicted.value());
  }
  ASSERT_EQ(0, replacer.Size());
}

TEST(LRUKReplacerTest, ShardingReplacer_Basic) {
  { /**
     * 需要 ShardingReplacer 仅 1 分片.
     */
    ShardingLRUKReplacer replacer{5, 3};

    replacer.Pin(0);

    replacer.RecordAccess(1);
    replacer.RecordAccess(2);
    replacer.RecordAccess(3);
    replacer.RecordAccess(4);

    auto select1{replacer.SelectEvictAndPin()};
    auto select2{replacer.SelectEvictAndPin()};
    ASSERT_EQ(select1.has_value(), true);
    ASSERT_EQ(select1.value(), 1);
    ASSERT_EQ(select2.has_value(), true);
    ASSERT_EQ(select2.value(), 2);

    auto res2{replacer.TryCommitEvict(2)};
    ASSERT_EQ(res2, true);
    auto res1{replacer.TryCommitEvict(1)};
    ASSERT_EQ(res1, true);

    // 1 and 2 are Still pinned.

    auto select3{replacer.SelectEvictAndPin()};
    ASSERT_EQ(select3.has_value(), true);
    ASSERT_EQ(select3.value(), 3);

    replacer.Unpin(3);

    select3 = replacer.SelectEvictAndPin();
    ASSERT_EQ(select3.has_value(), true);
    ASSERT_EQ(select3.value(), 3);
    replacer.Pin(3);
    replacer.Pin(3);
    replacer.Unpin(3);
    replacer.Unpin(3);
    auto res3{replacer.TryCommitEvict(3)};
    ASSERT_EQ(res3, true);

    auto select4{replacer.SelectEvictAndPin()};
    ASSERT_EQ(select4.has_value(), true);
    ASSERT_EQ(select4.value(), 4);
    auto res4{replacer.TryCommitEvict(4)};
    ASSERT_EQ(res4, true);

    auto select5{replacer.SelectEvictAndPin()};
    ASSERT_EQ(select5.has_value(), false);

    replacer.Unpin(1);

    select1 = replacer.SelectEvictAndPin();
    ASSERT_EQ(select1.has_value(), true);
    ASSERT_EQ(select1.value(), 1);
    replacer.Unpin(1);
  }

  {
    ShardingLRUKReplacer replacer{5, 3};

    for (int i = 0; i < 5; i++) {
      auto select{replacer.SelectEvictAndPin()};
      ASSERT_EQ(select.has_value(), true);
    }

    auto select{replacer.SelectEvictAndPin()};
    ASSERT_EQ(select.has_value(), false);
  }
}

TEST(LRUKReplacerTest, ShardingReplacer_LRUK) {
  /**
   * 需要 ShardingReplacer 仅 1 分片.
   */
  ShardingLRUKReplacer replacer{5, 3};

  replacer.Pin(0);

  replacer.RecordAccess(1);
  replacer.RecordAccess(1);
  replacer.RecordAccess(1);

  replacer.RecordAccess(3);
  replacer.RecordAccess(3);
  replacer.RecordAccess(3);

  replacer.RecordAccess(4);

  auto select2{replacer.SelectEvictAndPin()};
  ASSERT_EQ(select2.has_value(), true);
  ASSERT_EQ(select2.value(), 2);

  auto select4{replacer.SelectEvictAndPin()};
  ASSERT_EQ(select4.has_value(), true);
  ASSERT_EQ(select4.value(), 4);

  replacer.RecordAccess(4);
  replacer.RecordAccess(4);

  auto select1{replacer.SelectEvictAndPin()};
  ASSERT_EQ(select1.has_value(), true);
  ASSERT_EQ(select1.value(), 1);
}

TEST(LRUKReplacerTest, ShardingReplacer_Deadlock) {
  /**
   * 在执行测试前, 将 ShardingReplacer 的 `MAX_FRAME_PER_SHARD` 改为 4.
   */
  ShardingLRUKReplacer replacer{16, 4};

  // Init replacer with all evictable frames.
  for (std::size_t i = 0; i < 16; i++) {
    replacer.Pin(i);
    replacer.Unpin(i);
  }

  std::vector<std::thread> t(2);

  int iter_times{1000};

  auto work{[&]() {
    for (int iter = 0; iter < iter_times; iter++) {
      std::array<std::optional<frame_id_t>, 8> arr{};
      for (std::size_t i = 0; i < 8; i++) {
        arr[i] = replacer.SelectEvictAndPin();
        ASSERT_EQ(arr[i].has_value(), true);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
      for (std::size_t i = 0; i < 8; i++) {
        replacer.Unpin(arr[i].value());
      }
    }
  }};

  t[0] = std::thread{work};
  t[1] = std::thread{work};

  t[0].join();
  t[1].join();
}

}  // namespace bustub
