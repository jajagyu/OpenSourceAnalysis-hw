#include "sstable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {
struct SSTableHeader {
  bool has_metadata = false;
  int smallest_key = 0;
  int largest_key = 0;
  int64_t oldest_seq = 0;
  int64_t newest_seq = 0;
  bool has_bloom_filter = false;
  BloomFilter bloom_filter;
};

BloomFilter BuildBloomFilter(const std::vector<SSTableEntry>& entries,
                             size_t bits_per_key, size_t hash_count) {
  std::vector<int> keys;
  keys.reserve(entries.size());
  for (const auto& entry : entries) {
    keys.push_back(entry.key);
  }
  return BuildBloomFilterFromKeys(keys, bits_per_key, hash_count);
}

bool ReadSSTableHeader(std::ifstream* in, SSTableHeader* header) {
  
  // SSTable header를 읽는 함수.
  // Bloom filter bonus 진행시 bloom bits를 읽는 작업도 진행

  return true;
}

bool ParseSSTableDataLine(const std::string& line, SSTableEntry* entry) {
  std::istringstream iss(line);
  std::string op;
  if (!(iss >> op)) {
    return false;
  }

  if (op == "P") {
    // Put operation
  }

  if (op == "D") {
    // Delete operation
  }

  return false;
}

std::optional<uint64_t> ParseSSTFileId(const std::string& name) {
  // SSTable 이름 parsing
  const std::string prefix = "sst_";
  const std::string suffix = ".txt";
  if (name.size() <= prefix.size() + suffix.size()) {
    return std::nullopt;
  }
  if (name.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  if (name.substr(name.size() - suffix.size()) != suffix) {
    return std::nullopt;
  }
  const std::string number = name.substr(
      prefix.size(), name.size() - prefix.size() - suffix.size());
  if (number.empty()) {
    return std::nullopt;
  }
  for (char c : number) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
  }
  return static_cast<uint64_t>(std::stoull(number));
}

SSTableHeader BuildHeaderFromEntries(const std::vector<SSTableEntry>& entries,
                                     bool has_bloom_filter,
                                     BloomFilter bloom_filter) {
  // entry를 기반으로 SSTable header를 작성하는 함수. 필요시 사용
  SSTableHeader header;
  header.has_bloom_filter = has_bloom_filter;
  header.bloom_filter = std::move(bloom_filter);
  if (entries.empty()) {
    throw std::invalid_argument("cannot write empty SSTable");
  }

  header.has_metadata = true;
  header.smallest_key = entries.front().key;
  header.largest_key = entries.front().key;
  header.oldest_seq = entries.front().seq;
  header.newest_seq = entries.front().seq;
  for (const auto& entry : entries) {
    header.smallest_key = std::min(header.smallest_key, entry.key);
    header.largest_key = std::max(header.largest_key, entry.key);
    header.oldest_seq = std::min(header.oldest_seq, entry.seq);
    header.newest_seq = std::max(header.newest_seq, entry.seq);
  }
  return header;
}
}  // namespace

// *****

void EnsureSSTDir(const std::string& sst_dir) {
  std::filesystem::create_directories(sst_dir);
}

bool IsSSTDirEmpty(const std::string& sst_dir) {
  EnsureSSTDir(sst_dir);
  return std::filesystem::directory_iterator(sst_dir) ==
         std::filesystem::directory_iterator();
}

// ***** Directory open시 사용하는 함수.

std::vector<SSTableFile> ListSSTables(const std::string& sst_dir,
                                     bool load_bloom_filter) {
  EnsureSSTDir(sst_dir);
  std::vector<SSTableFile> out;

  // sst_dir의 SSTable을 읽고 SSTableFile struct vector에 등록하는 함수

  return out;
}

SSTableFile WriteSSTable(const std::string& sst_dir, uint64_t file_id,
                         const std::vector<SSTableEntry>& entries,
                         bool write_bloom_filter, size_t bloom_bits_per_key,
                         size_t bloom_hash_count) {
  SSTableFile file;

  // 주어진 entries에서 SSTable을 작성하는 함수

  return file;
}

bool GetFromSSTable(const SSTableFile& file, int key, std::string* value,
                    bool* tombstone) {

  // SSTable에서 key를 읽는 함수
  // Bloom filter 존재시 먼저 읽고 탐색을 진행해야 함

  return false;
}

std::vector<SSTableEntry> RangeScanSSTable(const SSTableFile& file,
                                           int start_key, int end_key) {
  
  // SSTable에서 Range scan을 진행하는 함수

  std::vector<SSTableEntry> out;

  return out;
}
