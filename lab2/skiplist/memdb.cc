#include "memdb.h"

#include <map>
#include <unordered_set>
#include <utility>

// SkipList를 사용하여 Out-of-place update를 진행하는 InMemoryDB
// Memtable 크기를 지정하여 가득 찼을 시 Immutable Memtable로 변경

InMemoryDB::MemTable::MemTable(const MemDBOptions& options)
    : list(options.skiplist_max_height, options.skiplist_p), size_bytes(0),
      immutable(false) {}

InMemoryDB::InMemoryDB(const MemDBOptions& options)
    : options_(options), mutable_(std::make_unique<MemTable>(options_)) {}

// Put operation 구현
// sequence number 구현 필요
void InMemoryDB::Put(int key, const std::string& value) {
  size_t entry_bytes = EntryBytes(key, value);
  EnsureMutableCapacity(entry_bytes);
  
  mutable_->list.Put(key, value);
  mutable_->size_bytes += entry_bytes;
}

// Get operation 구현
bool InMemoryDB::Get(int key, std::string* out_value) const {
  // First check mutable memtable
  if (mutable_->list.Get(key, out_value)) {
    return true;
  }
  
  // Then check immutable memtables in reverse order (most recent first)
  for (auto it = immutables_.rbegin(); it != immutables_.rend(); ++it) {
    if ((*it)->list.Get(key, out_value)) {
      return true;
    }
  }
  
  return false;
}

// Delete operation 구현. Tombstone 사용
void InMemoryDB::Delete(int key) {
  mutable_->list.Delete(key);
}

// RangeScan operation 구현
std::vector<std::pair<int, std::string>>
InMemoryDB::RangeScan(int start_key, int end_key) const {
  std::map<int, std::string> result;
  
  // Scan immutable memtables first (oldest first)
  for (auto it = immutables_.begin(); it != immutables_.end(); ++it) {
    std::vector<std::pair<int, std::string>> scan = (*it)->list.RangeScan(start_key, end_key);
    for (const auto& pair : scan) {
      result[pair.first] = pair.second;
    }
  }
  
  // Then scan mutable memtable (most recent)
  std::vector<std::pair<int, std::string>> mutable_scan = mutable_->list.RangeScan(start_key, end_key);
  for (const auto& pair : mutable_scan) {
    result[pair.first] = pair.second;
  }
  
  std::vector<std::pair<int, std::string>> out;
  for (const auto& pair : result) {
    out.push_back(pair);
  }
  
  return out;
}

// Memtable size 제한 확인하는 함수
void InMemoryDB::EnsureMutableCapacity(size_t entry_bytes) {
  if (mutable_->size_bytes + entry_bytes > options_.max_memtable_bytes) {
    // Mark current mutable as immutable and create new mutable
    mutable_->immutable = true;
    immutables_.push_back(std::move(mutable_));
    mutable_ = std::make_unique<MemTable>(options_);
  }
}

// 필요시 사용
size_t InMemoryDB::ImmutableCount() const { return immutables_.size(); }

size_t InMemoryDB::MutableSizeBytes() const { return mutable_->size_bytes; }

size_t InMemoryDB::EntryBytes(int key, const std::string& value) const {
  return sizeof(key) + value.size();
}
