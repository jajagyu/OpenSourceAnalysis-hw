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
  SkipList::RangeEntry entry;

  // First check mutable memtable
  if (mutable_->list.GetLatestEntry(key, &entry)) {
    if (entry.tombstone) {
      return false;
    }
    *out_value = entry.value;
    return true;
  }
  
  // Then check immutable memtables in reverse order (most recent first)
  for (auto it = immutables_.rbegin(); it != immutables_.rend(); ++it) {
    if ((*it)->list.GetLatestEntry(key, &entry)) {
      if (entry.tombstone) {
        return false;
      }
      *out_value = entry.value;
      return true;
    }
  }
  
  return false;
}

// Delete operation 구현. Tombstone 사용
void InMemoryDB::Delete(int key) {
  size_t entry_bytes = sizeof(key);
  EnsureMutableCapacity(entry_bytes);
  mutable_->list.Delete(key);
  mutable_->size_bytes += entry_bytes;
}

// RangeScan operation 구현
std::vector<std::pair<int, std::string>>
InMemoryDB::RangeScan(int start_key, int end_key) const {
  std::map<int, std::string> result;
  std::unordered_set<int> decided;

  // Newest mutable first: first seen key decides (value or tombstone).
  std::vector<SkipList::RangeEntry> mutable_entries =
      mutable_->list.RangeScanEntries(start_key, end_key);
  for (const auto& e : mutable_entries) {
    decided.insert(e.key);
    if (!e.tombstone) {
      result[e.key] = e.value;
    }
  }

  // Then older immutable memtables from newest to oldest.
  for (auto it = immutables_.rbegin(); it != immutables_.rend(); ++it) {
    std::vector<SkipList::RangeEntry> entries =
        (*it)->list.RangeScanEntries(start_key, end_key);
    for (const auto& e : entries) {
      if (decided.find(e.key) != decided.end()) {
        continue;
      }
      decided.insert(e.key);
      if (!e.tombstone) {
        result[e.key] = e.value;
      }
    }
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
