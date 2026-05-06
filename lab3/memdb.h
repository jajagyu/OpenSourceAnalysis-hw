#ifndef LAB3_MEMDB_H
#define LAB3_MEMDB_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "skiplist.h"
#include "sstable.h"

struct MemDBOptions {
  size_t max_memtable_bytes = 4 * 1024 * 1024;
  float skiplist_p = 0.5f;
  int skiplist_max_height = 16;
  std::string sst_dir = "sst";
  bool use_existing_db = false;
  bool enable_sstable_bloom_filter = false;
  size_t sstable_bloom_bits_per_key = 10;
  size_t sstable_bloom_hash_count = 6;
  bool enable_compaction = true;
  size_t compaction_file_threshold = 2;
};

class LSMDB {
 public:
  explicit LSMDB(const MemDBOptions& options);

  void Put(int key, const std::string& value);
  bool Get(int key, std::string* out_value) const;
  void Delete(int key);
  std::vector<std::pair<int, std::string>> RangeScan(int start_key,
                                                     int end_key) const;

  size_t ImmutableCount() const;
  size_t FlushedFileCount() const;
  size_t MutableSizeBytes() const;

 private:
  struct MemTable {
    explicit MemTable(const MemDBOptions& options);

    SkipList list;
    size_t size_bytes;
    bool immutable;
  };

  void EnsureMutableCapacity(size_t entry_bytes);
  size_t EntryBytes(int key, const std::string& value) const;

  void FlushAllImmutables();
  void FlushOneImmutable(const MemTable* table);
  void MaybeCompactSSTables();

  MemDBOptions options_;
  std::unique_ptr<MemTable> mutable_;
  std::vector<std::unique_ptr<MemTable>> immutables_;
  std::vector<SSTableFile> flushed_files_;
  uint64_t next_file_id_;
  int64_t next_seq_;
};

#endif  // LAB3_MEMDB_H
