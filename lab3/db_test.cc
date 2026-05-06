#include "memdb.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace {
std::vector<std::string> g_test_sst_dirs;

std::vector<std::pair<int, std::string>> LoadWorkload(const std::string& path) {
  std::ifstream in(path);
  std::vector<std::pair<int, std::string>> out;
  int key = 0;
  std::string value;
  while (in >> key >> value) {
    out.emplace_back(key, value);
  }
  return out;
}

std::vector<int> SortedKeys(const std::map<int, std::string>& expected) {
  std::vector<int> keys;
  keys.reserve(expected.size());
  for (const auto& kv : expected) {
    keys.push_back(kv.first);
  }
  return keys;
}

std::vector<int> LoadKeyList(const std::string& path) {
  std::ifstream in(path);
  std::vector<int> keys;
  int key = 0;
  while (in >> key) {
    keys.push_back(key);
  }
  return keys;
}

struct WorkloadOp {
  char type;
  int key;
  std::string value;
};

std::vector<WorkloadOp> LoadWorkloadOps(const std::string& path) {
  std::ifstream in(path);
  std::vector<WorkloadOp> ops;
  std::string tag;
  while (in >> tag) {
    if (tag == "P") {
      int key = 0;
      std::string value;
      if (!(in >> key >> value)) {
        break;
      }
      ops.push_back(WorkloadOp{'P', key, value});
    } else if (tag == "D") {
      int key = 0;
      if (!(in >> key)) {
        break;
      }
      ops.push_back(WorkloadOp{'D', key, ""});
    } else {
      // Fallback: key value format.
      int key = std::stoi(tag);
      std::string value;
      if (!(in >> value)) {
        break;
      }
      ops.push_back(WorkloadOp{'P', key, value});
    }
  }
  return ops;
}

MemDBOptions NewTestOptions() {
  static int id = 0;
  MemDBOptions options;
  options.use_existing_db = false;
  options.sst_dir = "sst_test_" + std::to_string(id++);
  g_test_sst_dirs.push_back(options.sst_dir);
  std::filesystem::remove_all(options.sst_dir);
  return options;
}

MemDBOptions NewBloomTestOptions() {
  MemDBOptions options = NewTestOptions();
  options.enable_sstable_bloom_filter = true;
  options.sstable_bloom_bits_per_key = 8;
  return options;
}

void PrintProgress(const char* label, size_t checked, size_t total) {
  if (total == 0) {
    return;
  }
  const size_t step = std::max<size_t>(1, total / 20);
  if (checked % step == 0 || checked == total) {
    std::cerr << "[" << label << "] checked " << checked << "/" << total
              << " (" << (checked * 100 / total) << "%)\n";
  }
}

void VerifyPutWorkload(const MemDBOptions& options, const std::string& path,
                       const char* label) {
  LSMDB db(options);

  auto workload = LoadWorkload(path);
  ASSERT_FALSE(workload.empty());

  std::map<int, std::string> expected;
  for (const auto& kv : workload) {
    db.Put(kv.first, kv.second);
    expected[kv.first] = kv.second;
  }

  size_t checked = 0;
  for (int key : SortedKeys(expected)) {
    std::string value = "<not read>";
    bool found = db.Get(key, &value);
    ASSERT_TRUE(found) << "key=" << key << " expected=" << expected[key]
                       << " actual=<not found>";
    ASSERT_EQ(value, expected[key])
        << "key=" << key << " expected=" << expected[key]
        << " actual=" << value;
    ++checked;
    if (label) {
      PrintProgress(label, checked, expected.size());
    }
  }
}

void VerifyPutDeleteWorkload(const MemDBOptions& options, const std::string& path,
                             const char* label) {
  LSMDB db(options);

  auto ops = LoadWorkloadOps(path);
  ASSERT_FALSE(ops.empty());

  std::map<int, std::string> expected;
  for (const auto& op : ops) {
    if (op.type == 'P') {
      db.Put(op.key, op.value);
      expected[op.key] = op.value;
    } else if (op.type == 'D') {
      db.Delete(op.key);
      expected.erase(op.key);
    }
  }

  size_t checked = 0;
  for (const auto& kv : expected) {
    std::string value = "<not read>";
    bool found = db.Get(kv.first, &value);
    ASSERT_TRUE(found) << "key=" << kv.first << " expected=" << kv.second
                       << " actual=<not found>";
    ASSERT_EQ(value, kv.second)
        << "key=" << kv.first << " expected=" << kv.second
        << " actual=" << value;
    ++checked;
    if (label) {
      PrintProgress(label, checked, expected.size());
    }
  }
}

void VerifyAbsentReadWorkload(const MemDBOptions& options,
                              const std::string& put_path,
                              const std::string& miss_path,
                              const char* label) {
  LSMDB db(options);

  auto workload = LoadWorkload(put_path);
  ASSERT_FALSE(workload.empty());
  for (const auto& kv : workload) {
    db.Put(kv.first, kv.second);
  }

  auto missing_keys = LoadKeyList(miss_path);
  ASSERT_FALSE(missing_keys.empty());

  size_t checked = 0;
  for (int key : missing_keys) {
    std::string value = "<not read>";
    ASSERT_FALSE(db.Get(key, &value))
        << "expected missing key=" << key << " to be absent";
    ++checked;
    if (label) {
      PrintProgress(label, checked, missing_keys.size());
    }
  }
}
} // namespace

TEST(MemDBTest, PutGetBasic) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1024;
  options.skiplist_max_height = 8;
  options.skiplist_p = 0.5f;

  LSMDB db(options);
  db.Put(1, "one");
  db.Put(2, "two");

  std::string value;
  EXPECT_TRUE(db.Get(1, &value));
  EXPECT_EQ(value, "one");
  EXPECT_TRUE(db.Get(2, &value));
  EXPECT_EQ(value, "two");
  EXPECT_FALSE(db.Get(3, &value));
}

TEST(MemDBTest, DeleteMasksOlderValue) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1024;
  LSMDB db(options);

  db.Put(1, "v1");
  db.Put(2, "v2");
  db.Delete(1);

  std::string value;
  EXPECT_FALSE(db.Get(1, &value));
  EXPECT_TRUE(db.Get(2, &value));
  EXPECT_EQ(value, "v2");
}

TEST(MemDBTest, ImmutableSearchOrder) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12; // sizeof(int)=4, value size=4 -> entry 8
  LSMDB db(options);

  db.Put(1, "aaaa"); // mutable size 8
  db.Put(2, "bbbb"); // rotates, then inserts into new mutable

  EXPECT_EQ(db.ImmutableCount(), 0u);
  EXPECT_EQ(db.FlushedFileCount(), 1u);

  std::string value;
  EXPECT_TRUE(db.Get(1, &value));
  EXPECT_EQ(value, "aaaa");
  EXPECT_TRUE(db.Get(2, &value));
  EXPECT_EQ(value, "bbbb");
}

TEST(MemDBTest, TombstoneStopsOlderLookup) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12; // force rotation on second insert
  LSMDB db(options);

  db.Put(1, "old");  // goes into first memtable
  db.Put(2, "fill"); // rotates to new memtable
  db.Delete(1);      // tombstone in newest memtable

  std::string value;
  EXPECT_FALSE(db.Get(1, &value));
  EXPECT_TRUE(db.Get(2, &value));
  EXPECT_EQ(value, "fill");
}

TEST(MemDBTest, MultipleVersionsSameKey) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  LSMDB db(options);

  db.Put(7, "v1");
  db.Put(7, "v2");
  db.Put(7, "v3");

  std::string value;
  ASSERT_TRUE(db.Get(7, &value));
  ASSERT_EQ(value, "v3") << "expected newest version for same key";
}

TEST(MemDBTest, TombstoneMasksOlderVersions) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  LSMDB db(options);

  db.Put(7, "v1");
  db.Put(7, "v2");
  db.Put(8, "v8");
  db.Delete(7);

  std::string value;
  ASSERT_TRUE(db.Get(8, &value));
  ASSERT_EQ(value, "v8");
  ASSERT_FALSE(db.Get(7, &value)) << "tombstone should hide older versions";
}

TEST(MemDBCompactionTest, CompactsAllSSTablesAndKeepsLatestValues) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12;
  options.compaction_file_threshold = 2;
  LSMDB db(options);

  db.Put(1, "aaaa");
  db.Put(2, "bbbb"); // flushes key 1
  db.Put(1, "cccc"); // flushes key 2
  db.Put(3, "dddd"); // flushes newer key 1 and triggers compaction

  ASSERT_EQ(db.FlushedFileCount(), 1u);

  std::string value;
  ASSERT_TRUE(db.Get(1, &value));
  ASSERT_EQ(value, "cccc");
  ASSERT_TRUE(db.Get(2, &value));
  ASSERT_EQ(value, "bbbb");
  ASSERT_TRUE(db.Get(3, &value));
  ASSERT_EQ(value, "dddd");

  const auto files = ListSSTables(options.sst_dir);
  ASSERT_EQ(files.size(), 1u);
}

TEST(MemDBCompactionTest, DropsTombstonesAfterFullCompaction) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12;
  options.compaction_file_threshold = 2;
  LSMDB db(options);

  db.Put(1, "old");
  db.Put(2, "fill"); // flushes key 1
  db.Delete(1);
  db.Put(3, "cccc"); // flushes tombstone for key 1 and key 2
  db.Put(4, "dddd"); // flushes key 3 and triggers compaction

  ASSERT_EQ(db.FlushedFileCount(), 1u);

  std::string value;
  ASSERT_FALSE(db.Get(1, &value));
  ASSERT_TRUE(db.Get(2, &value));
  ASSERT_EQ(value, "fill");
  ASSERT_TRUE(db.Get(3, &value));
  ASSERT_EQ(value, "cccc");

  const auto files = ListSSTables(options.sst_dir);
  ASSERT_EQ(files.size(), 1u);
  auto entries = RangeScanSSTable(files.front(), std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max());
  for (const auto& entry : entries) {
    ASSERT_NE(entry.key, 1);
    ASSERT_FALSE(entry.tombstone);
  }
}

TEST(MemDBCompactionTest, UsesSequenceNumbersWhenCompacting) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12;
  options.compaction_file_threshold = 2;
  LSMDB db(options);

  db.Put(1, "aaaa"); // seq 1
  db.Put(2, "bbbb"); // seq 2, flushes key 1
  db.Put(1, "cccc"); // seq 3, flushes key 2
  db.Put(3, "dddd"); // seq 4, flushes newer key 1 and triggers compaction

  const auto files = ListSSTables(options.sst_dir);
  ASSERT_EQ(files.size(), 1u);
  auto entries = RangeScanSSTable(files.front(), std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max());

  bool saw_key_1 = false;
  for (const auto& entry : entries) {
    if (entry.key == 1) {
      saw_key_1 = true;
      ASSERT_EQ(entry.seq, 3);
      ASSERT_EQ(entry.value, "cccc");
    }
  }
  ASSERT_TRUE(saw_key_1);
}

TEST(MemDBCompactionTest, RebuildsBloomFilterForCompactedSSTable) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 12;
  options.compaction_file_threshold = 2;
  LSMDB db(options);

  db.Put(1, "aaaa");
  db.Put(2, "bbbb"); // flushes key 1
  db.Put(1, "cccc"); // flushes key 2
  db.Put(3, "dddd"); // flushes newer key 1 and triggers compaction

  const auto files = ListSSTables(options.sst_dir, true);
  ASSERT_EQ(files.size(), 1u);
  ASSERT_TRUE(files.front().has_bloom_filter);
  ASSERT_FALSE(files.front().bloom_filter.Empty());
  ASSERT_TRUE(files.front().bloom_filter.MayContain(1));
  ASSERT_TRUE(files.front().bloom_filter.MayContain(2));
}

TEST(MemDBReopenTest, LoadsSSTableMetadataAndReadsExistingDB) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 12;
  options.compaction_file_threshold = 2;

  {
    LSMDB db(options);
    db.Put(1, "aaaa");
    db.Put(2, "bbbb"); // flushes key 1
    db.Put(1, "cccc"); // flushes key 2
    db.Put(3, "dddd"); // flushes newer key 1 and triggers compaction
  }

  auto files = ListSSTables(options.sst_dir, true);
  ASSERT_EQ(files.size(), 1u);
  ASSERT_EQ(files.front().smallest_key, 1);
  ASSERT_EQ(files.front().largest_key, 2);
  ASSERT_EQ(files.front().oldest_seq, 2);
  ASSERT_EQ(files.front().newest_seq, 3);
  ASSERT_TRUE(files.front().has_bloom_filter);
  ASSERT_FALSE(files.front().bloom_filter.Empty());

  MemDBOptions reopen_options = options;
  reopen_options.use_existing_db = true;
  LSMDB reopened(reopen_options);

  std::string value;
  ASSERT_TRUE(reopened.Get(1, &value));
  ASSERT_EQ(value, "cccc");
  ASSERT_TRUE(reopened.Get(2, &value));
  ASSERT_EQ(value, "bbbb");
  ASSERT_FALSE(reopened.Get(3, &value));
}

TEST(MemDBReopenTest, ContinuesSequenceNumbersAfterReopen) {
  MemDBOptions options = NewTestOptions();
  options.enable_compaction = false;
  options.max_memtable_bytes = 12;

  {
    LSMDB db(options);
    db.Put(1, "aaaa"); // seq 1
    db.Put(2, "bbbb"); // seq 2, flushes key 1
    db.Put(1, "cccc"); // seq 3, flushes key 2
    db.Put(3, "dddd"); // seq 4, flushes newer key 1
  }

  MemDBOptions reopen_options = options;
  reopen_options.use_existing_db = true;
  {
    LSMDB reopened(reopen_options);
    reopened.Put(1, "eeee"); // should continue at seq 4
    reopened.Put(4, "ffff"); // flushes newer key 1
  }

  auto files = ListSSTables(options.sst_dir);
  bool saw_newer_key_1 = false;
  for (const auto& file : files) {
    auto entries = RangeScanSSTable(file, std::numeric_limits<int>::min(),
                                    std::numeric_limits<int>::max());
    for (const auto& entry : entries) {
      if (entry.key == 1 && entry.value == "eeee") {
        saw_newer_key_1 = true;
        ASSERT_EQ(entry.seq, 4);
      }
    }
  }
  ASSERT_TRUE(saw_newer_key_1);
}

TEST(MemDBWorkloadTest, FillRandom) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillrandom.txt", "FillRandom");
}

TEST(MemDBWorkloadTest, FillSequential) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillseq.txt", nullptr);
}

TEST(MemDBWorkloadTest, RangeScanAcrossMemtables) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 12; // small to force rotation
  LSMDB db(options);

  db.Put(1, "v1");
  db.Put(2, "v2");
  db.Put(3, "v3");
  db.Put(4, "v4"); // rotates
  db.Delete(3);    // tombstone in newest memtable
  db.Put(5, "v5");

  auto result = db.RangeScan(1, 5);
  std::map<int, std::string> expected = {
      {1, "v1"}, {2, "v2"}, {4, "v4"}, {5, "v5"}};

  ASSERT_EQ(result.size(), expected.size());
  for (const auto& kv : result) {
    auto it = expected.find(kv.first);
    ASSERT_TRUE(it != expected.end()) << "unexpected key=" << kv.first;
    ASSERT_EQ(kv.second, it->second) << "key=" << kv.first;
  }
}

TEST(MemDBWorkloadTest, FillRandom2) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillrandom_2.txt", "FillRandom2");
}

TEST(MemDBWorkloadTest, FillDeleteRandom) {
  MemDBOptions options = NewTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutDeleteWorkload(options, "workloads/filldelete_random.txt",
                          "FillDeleteRandom");
}

TEST(MemDBWorkloadTest, ReadMissingBloomFriendly) {
  MemDBOptions options = NewTestOptions();
  options.enable_compaction = false;
  options.max_memtable_bytes = 8 * 1024;
  VerifyAbsentReadWorkload(options, "workloads/fillrandom_bloom.txt",
                           "workloads/readmissing_bloom.txt",
                           "ReadMissingBloomFriendly");
}

TEST(MemDBBloomWorkloadTest, FillRandom) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillrandom.txt", "BloomFillRandom");
}

TEST(MemDBBloomWorkloadTest, FillSequential) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillseq.txt", nullptr);
}

TEST(MemDBBloomWorkloadTest, FillRandom2) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutWorkload(options, "workloads/fillrandom_2.txt", "BloomFillRandom2");
}

TEST(MemDBBloomWorkloadTest, FillDeleteRandom) {
  MemDBOptions options = NewBloomTestOptions();
  options.max_memtable_bytes = 1 << 20;
  VerifyPutDeleteWorkload(options, "workloads/filldelete_random.txt",
                          "BloomFillDeleteRandom");
}

TEST(MemDBBloomWorkloadTest, ReadMissingBloomFriendly) {
  MemDBOptions options = NewBloomTestOptions();
  options.enable_compaction = false;
  options.max_memtable_bytes = 8 * 1024;
  VerifyAbsentReadWorkload(options, "workloads/fillrandom_bloom.txt",
                           "workloads/readmissing_bloom.txt",
                           "BloomReadMissingFriendly");
}

TEST(MemDBInfoTest, BloomFilterIntegratedSSTableFormat) {
  MemDBOptions options = NewTestOptions();
  options.enable_sstable_bloom_filter = true;
  options.sstable_bloom_bits_per_key = 8;
  options.max_memtable_bytes = 12; // force a flush quickly

  LSMDB db(options);
  db.Put(10, "aaaa");
  db.Put(20, "bbbb"); // flushes key 10 into SST
  db.Put(30, "cccc");

  std::string value;
  ASSERT_TRUE(db.Get(10, &value));
  ASSERT_EQ(value, "aaaa");

  const auto files = ListSSTables(options.sst_dir, true);
  ASSERT_FALSE(files.empty());

  const auto& file = files.front();
  ASSERT_TRUE(file.has_bloom_filter);
  ASSERT_FALSE(file.bloom_filter.Empty());
  ASSERT_TRUE(file.bloom_filter.MayContain(10));

  std::ifstream in(file.path);
  ASSERT_TRUE(in.is_open());

  std::string meta_tag;
  int bloom_enabled = 0;
  int smallest_key = 0;
  int largest_key = 0;
  int64_t oldest_seq = 0;
  int64_t newest_seq = 0;
  ASSERT_TRUE(in >> meta_tag >> bloom_enabled >> smallest_key >> largest_key >>
              oldest_seq >> newest_seq);
  ASSERT_EQ(meta_tag, "META");
  ASSERT_EQ(bloom_enabled, 1);
  ASSERT_EQ(smallest_key, 10);
  ASSERT_EQ(largest_key, 10);
  ASSERT_EQ(oldest_seq, 1);
  ASSERT_EQ(newest_seq, 1);

  std::string bloom_tag;
  size_t bit_count = 0;
  size_t hash_count = 0;
  std::string encoded_bits;
  ASSERT_TRUE(in >> bloom_tag >> bit_count >> hash_count >> encoded_bits);
  ASSERT_EQ(bloom_tag, "BLOOM");
  ASSERT_GT(bit_count, 0u);
  ASSERT_GT(hash_count, 0u);
  ASSERT_FALSE(encoded_bits.empty());
}

TEST(MemDBInfoTest, BloomFilterNegativeMeansKeyAbsentFromSSTable) {
  MemDBOptions options = NewTestOptions();
  options.enable_sstable_bloom_filter = true;
  options.sstable_bloom_bits_per_key = 8;
  options.max_memtable_bytes = 12; // force flush quickly

  LSMDB db(options);
  db.Put(10, "aaaa"); // flushed on next put
  db.Put(20, "bbbb");
  db.Put(30, "cccc");

  const auto files = ListSSTables(options.sst_dir, true);
  ASSERT_FALSE(files.empty());

  const auto& file = files.front();
  ASSERT_TRUE(file.has_bloom_filter);
  ASSERT_FALSE(file.bloom_filter.Empty());
  ASSERT_TRUE(file.bloom_filter.MayContain(10));

  int negative_checks = 0;
  for (int key = 1000; key < 1200; ++key) {
    if (file.bloom_filter.MayContain(key)) {
      continue;
    }

    std::string value;
    bool tombstone = false;
    ASSERT_FALSE(GetFromSSTable(file, key, &value, &tombstone))
        << "bloom filter returned false, but SSTable lookup found key="
        << key;
    ++negative_checks;
  }

  ASSERT_GT(negative_checks, 0)
      << "did not find any bloom-filter negatives to validate";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int rc = RUN_ALL_TESTS();
  for (const auto& dir : g_test_sst_dirs) {
    std::filesystem::remove_all(dir);
  }
  return rc;
}
