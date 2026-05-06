#include "compaction.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
void RemoveSSTableFile(const SSTableFile& file) {

  // Compaction 완료 후 SSTable 삭제
}
} // namespace

std::optional<SSTableFile>
CompactAllSSTables(const std::string& sst_dir,
                   const std::vector<SSTableFile>& files,
                   uint64_t output_file_id, bool write_bloom_filter,
                   size_t bloom_bits_per_key, size_t bloom_hash_count) {

  // 모든 SSTable Compaction 진행

  std::optional<SSTableFile> compacted_file;
  return compacted_file;
}
