#ifndef LAB3_COMPACTION_H
#define LAB3_COMPACTION_H

#include "sstable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

std::optional<SSTableFile> CompactAllSSTables(
    const std::string& sst_dir, const std::vector<SSTableFile>& files,
    uint64_t output_file_id, bool write_bloom_filter,
    size_t bloom_bits_per_key, size_t bloom_hash_count);

#endif  // LAB3_COMPACTION_H
