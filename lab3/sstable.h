#ifndef LAB3_SSTABLE_H
#define LAB3_SSTABLE_H

#include "bloom_filter.h"

#include <cstdint>
#include <string>
#include <vector>

struct SSTableFile {
  uint64_t id;
  std::string path;
  int smallest_key = 0;
  int largest_key = 0;
  int64_t oldest_seq = 0;
  int64_t newest_seq = 0;
  bool has_bloom_filter = false;
  BloomFilter bloom_filter;
};

struct SSTableEntry {
  int key;
  int64_t seq;
  std::string value;
  bool tombstone;
};

void EnsureSSTDir(const std::string& sst_dir);
bool IsSSTDirEmpty(const std::string& sst_dir);
std::vector<SSTableFile> ListSSTables(const std::string& sst_dir,
                                     bool load_bloom_filter = false);
SSTableFile WriteSSTable(const std::string& sst_dir, uint64_t file_id,
                         const std::vector<SSTableEntry>& entries,
                         bool write_bloom_filter = false,
                         size_t bloom_bits_per_key = 10,
                         size_t bloom_hash_count = 6);
bool GetFromSSTable(const SSTableFile& file, int key, std::string* value,
                    bool* tombstone);
std::vector<SSTableEntry> RangeScanSSTable(const SSTableFile& file,
                                           int start_key, int end_key);

#endif  // LAB3_SSTABLE_H
