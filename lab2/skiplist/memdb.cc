#include "memdb.h"

#include <map>
#include <unordered_set>
#include <utility>

// SkipList를 사용하여 Out-of-place update를 진행하는 InMemoryDB
// Memtable 크기를 지정하여 가득 찼을 시 Immutable Memtable로 변경

// MemDBOptions의 skiplist_p와 skiplist_max_height를 SkipList 생성자에 전달하여
// SkipList의 레벨과 승격 확률을 MemDB 옵션에 따라 설정
InMemoryDB::MemTable::MemTable(const MemDBOptions& options)
    : list(options.skiplist_max_height, options.skiplist_p), size_bytes(0),
      immutable(false) {}

// InMemoryDB 생성자에서 MemDBOptions를 받아 초기 mutable MemTable을 생성, SkipList의 옵션도 전달
InMemoryDB::InMemoryDB(const MemDBOptions& options)
    : options_(options), mutable_(std::make_unique<MemTable>(options_)) {}

// Put operation 구현
// sequence number 구현 필요
void InMemoryDB::Put(int key, const std::string& value) {
  // 새 버전 기록 전에 mutable memtable 용량 확보
  size_t entry_bytes = EntryBytes(key, value);
  EnsureMutableCapacity(entry_bytes);

  // 실제 쓰기는 mutable에서만 수행
  mutable_->list.Put(key, value);
  mutable_->size_bytes += entry_bytes;
}

// Get operation 구현
bool InMemoryDB::Get(int key, std::string* out_value) const {
  SkipList::RangeEntry entry;

  // mutable에서 먼저 최신 엔트리를 확인, tombstone이면 즉시 삭제로 확정
  // 최신 계층에서 tombstone을 발견하면 즉시 삭제로 확정
  if (mutable_->list.GetLatestEntry(key, &entry)) {
    if (entry.tombstone) {
      return false;
    }
    *out_value = entry.value;
    return true;
  }
  
  //mutable에서 key가 없으면 immutable 계층에서 최신 엔트리를 확인, tombstone이면 삭제로 확정
  // 오래된 계층의 값이 최신 tombstone을 덮어쓰지 않도록 최신->과거 순으로 확인
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
  // tombstone도 하나의 엔트리이므로 최소 key 크기만큼 용량을 사용 -> 용량 확보 필요
  size_t entry_bytes = sizeof(key);
  EnsureMutableCapacity(entry_bytes);
  mutable_->list.Delete(key);
  mutable_->size_bytes += entry_bytes;
}

// RangeScan operation 구현
std::vector<std::pair<int, std::string>>
InMemoryDB::RangeScan(int start_key, int end_key) const {
  // result: 최종 노출 값
  // decided: 이미 최신 계층에서 key의 운명(값 또는 tombstone)이 결정되었는지
  std::map<int, std::string> result;
  std::unordered_set<int> decided;

  // mutable에서 먼저 key를 확정하면, 이후 immutable에서는 같은 key를 무시
  std::vector<SkipList::RangeEntry> mutable_entries =
      mutable_->list.RangeScanEntries(start_key, end_key);
  for (const auto& e : mutable_entries) {
    decided.insert(e.key);
    if (!e.tombstone) {
      result[e.key] = e.value;
    }
  }

  // 최신 immutable부터 과거 immutable로 내려가며 아직 미확정 key만 반영
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
// mutable memtable의 용량이 충분한지 않으면 현재 mutable을 immutable로 고정하고 새 mutable을 생성
void InMemoryDB::EnsureMutableCapacity(size_t entry_bytes) {
  if (mutable_->size_bytes + entry_bytes > options_.max_memtable_bytes) {
    // 현재 mutable을 immutable로 고정하고 새 mutable을 생성한다.
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
