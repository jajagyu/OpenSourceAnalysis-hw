#include "memdb.h"

#include "compaction.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>

LSMDB::MemTable::MemTable(const MemDBOptions& options)
    : list(options.skiplist_max_height, options.skiplist_p), size_bytes(0),
      immutable(false) {}

LSMDB::LSMDB(const MemDBOptions& options)
    : options_(options), mutable_(std::make_unique<MemTable>(options_)),
      next_file_id_(1), next_seq_(1) {
  EnsureSSTDir(options_.sst_dir);
  if (options_.use_existing_db) {
    flushed_files_ =
        ListSSTables(options_.sst_dir, options_.enable_sstable_bloom_filter);
    if (!flushed_files_.empty()) {
      next_file_id_ = flushed_files_.back().id + 1;
    }
    int64_t max_seq = 0;
    for (const auto& file : flushed_files_) {
      max_seq = std::max(max_seq, file.newest_seq);
    }
    next_seq_ = max_seq + 1;
  } else if (!IsSSTDirEmpty(options_.sst_dir)) {
    throw std::runtime_error(
        "sst_dir is not empty; set use_existing_db=true to reuse it");
  }
} // DB Open 구현 기본 제공

void LSMDB::Put(int key, const std::string& value) {
  // DB에 제공되는 key-value 삽입
  // Memtable이 가득 차면 immutable로 바꾸고 Flush 진행
  
  // 필요한 바이트 크기 계산
  size_t entry_bytes = EntryBytes(key, value);
  
  // Mutable Memtable에 공간 확보 (필요시 Flush)
  EnsureMutableCapacity(entry_bytes);
  
  // Mutable Memtable에 데이터 추가
  mutable_->list.PutWithSequence(key, value, next_seq_);
  mutable_->size_bytes += entry_bytes;
  next_seq_++;
}

bool LSMDB::Get(int key, std::string* out_value) const {
  // DB내에 해당하는 key를 찾기
  // 검색 순서: Mutable -> Immutable -> SSTable
  
  // 1. Mutable Memtable에서 먼저 찾기
  std::string value;
  bool is_tombstone = false;
  
  if (mutable_->list.GetLatest(key, &value, &is_tombstone)) {
    // Mutable에서 찾음
    if (is_tombstone) {
      return false;  // 삭제된 데이터
    }
    *out_value = value;
    return true;
  }
  
  // 2. Immutable Memtables에서 찾기 (역순으로)
  for (int i = static_cast<int>(immutables_.size()) - 1; i >= 0; --i) {
    if (immutables_[i]->list.GetLatest(key, &value, &is_tombstone)) {
      // Immutable에서 찾음
      if (is_tombstone) {
        return false;  // 삭제된 데이터
      }
      *out_value = value;
      return true;
    }
  }
  
  // 3. SSTable에서 찾기 (역순으로, 최신 파일부터)
  for (int i = static_cast<int>(flushed_files_.size()) - 1; i >= 0; --i) {
    if (GetFromSSTable(flushed_files_[i], key, &value, &is_tombstone)) {
      // SSTable에서 찾음
      if (is_tombstone) {
        return false;  // 삭제된 데이터
      }
      *out_value = value;
      return true;
    }
  }
  
  return false;
}

void LSMDB::Delete(int key) {
  // DB내에 해당하는 key에 대해 tombstone(deletion marker) 삽입
  // Memtable이 가득 차면 immutable로 바꾸고 Flush 진행
  
  // Tombstone 삽입: 빈 value와 tombstone 플래그 사용
  size_t entry_bytes = EntryBytes(key, "");
  
  // Mutable Memtable에 공간 확보 (필요시 Flush)
  EnsureMutableCapacity(entry_bytes);
  
  // Mutable Memtable에 tombstone 추가
  mutable_->list.DeleteWithSequence(key, next_seq_);
  mutable_->size_bytes += entry_bytes;
  next_seq_++;
}

std::vector<std::pair<int, std::string>> LSMDB::RangeScan(int start_key,
                                                          int end_key) const {

  // DB내에 key 범위에 해당하는 key-value 쌍을 모두 찾아서 반환
  // 모든 계층(Mutable, Immutable, SSTable)에서 데이터를 수집하고
  // 최신 seq를 기준으로 중복 제거
  
  std::vector<std::pair<int, std::string>> out;
  std::map<int, std::pair<int64_t, std::pair<std::string, bool>>> latest_entries;
  // key -> (seq, (value, tombstone))
  
  // 1. Mutable Memtable에서 범위 스캔
  auto mutable_entries = mutable_->list.RangeScanLatest(start_key, end_key);
  for (const auto& entry : mutable_entries) {
    auto it = latest_entries.find(entry.key);
    if (it == latest_entries.end() || entry.seq > it->second.first) {
      latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
    }
  }
  
  // 2. Immutable Memtables에서 범위 스캔 (역순)
  for (int i = static_cast<int>(immutables_.size()) - 1; i >= 0; --i) {
    auto immutable_entries = immutables_[i]->list.RangeScanLatest(start_key, end_key);
    for (const auto& entry : immutable_entries) {
      auto it = latest_entries.find(entry.key);
      if (it == latest_entries.end() || entry.seq > it->second.first) {
        latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
      }
    }
  }
  
  // 3. SSTable에서 범위 스캔 (역순)
  for (int i = static_cast<int>(flushed_files_.size()) - 1; i >= 0; --i) {
    auto sstable_entries = RangeScanSSTable(flushed_files_[i], start_key, end_key);
    for (const auto& entry : sstable_entries) {
      auto it = latest_entries.find(entry.key);
      if (it == latest_entries.end() || entry.seq > it->second.first) {
        latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
      }
    }
  }
  
  // 최종 결과 구성: tombstone이 아닌 항목만 반환
  for (const auto& pair : latest_entries) {
    if (!pair.second.second.second) {  // !tombstone
      out.push_back({pair.first, pair.second.second.first});
    }
  }
  
  return out;
}

size_t LSMDB::ImmutableCount() const { return immutables_.size(); }

size_t LSMDB::FlushedFileCount() const { return flushed_files_.size(); }

size_t LSMDB::MutableSizeBytes() const { return mutable_->size_bytes; }

void LSMDB::EnsureMutableCapacity(size_t entry_bytes) {
  // Mutable Memtable의 사이즈를 체크하는 함수. 필요시 사용
  if (mutable_->size_bytes + entry_bytes <= options_.max_memtable_bytes) {
    return;
  }

  mutable_->immutable = true;
  immutables_.push_back(std::move(mutable_));
  mutable_ = std::make_unique<MemTable>(options_);

  // Flush as soon as immutable memtable exists.
  FlushAllImmutables();
}

size_t LSMDB::EntryBytes(int key, const std::string& value) const {
  // 각 entry의 크기 byte를 반환하는 함수. 필요시 사용
  return sizeof(key) + value.size();
}

void LSMDB::FlushAllImmutables() {
  while (!immutables_.empty()) {
    FlushOneImmutable(immutables_.front().get());
    immutables_.erase(immutables_.begin());
  }
}

void LSMDB::FlushOneImmutable(const MemTable* table) {
  // 하나의 Immutable Memtable을 SSTable로 flush하는 함수. 필요시 사용.
  const uint64_t file_id = next_file_id_++;
  std::vector<SSTableEntry> flush_entries;
  auto entries = table->list.RangeScanLatest(std::numeric_limits<int>::min(),
                                             std::numeric_limits<int>::max());
  flush_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    flush_entries.push_back(
        SSTableEntry{entry.key, entry.seq, entry.value, entry.tombstone});
  }

  flushed_files_.push_back(WriteSSTable(
      options_.sst_dir, file_id, flush_entries,
      options_.enable_sstable_bloom_filter, options_.sstable_bloom_bits_per_key,
      options_.sstable_bloom_hash_count));
  MaybeCompactSSTables();
}

void LSMDB::MaybeCompactSSTables() {
  // SSTable 개수를 검사해 Compaction을 진행하는 함수. 필요시 사용.
  if (!options_.enable_compaction || options_.compaction_file_threshold == 0 ||
      flushed_files_.size() <= options_.compaction_file_threshold) {
    return;
  }

  const uint64_t file_id = next_file_id_++;
  auto compacted_file = CompactAllSSTables(
      options_.sst_dir, flushed_files_, file_id,
      options_.enable_sstable_bloom_filter, options_.sstable_bloom_bits_per_key,
      options_.sstable_bloom_hash_count);

  flushed_files_.clear();
  if (compacted_file.has_value()) {
    flushed_files_.push_back(std::move(*compacted_file));
  }
}
