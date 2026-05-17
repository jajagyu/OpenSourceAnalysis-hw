/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#include "memdb.h"

#include "compaction.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>

// Memtable 생성자
// Skiplist 기반 정렬 저장소 초기화
LSMDB::MemTable::MemTable(const MemDBOptions& options)
    : list(options.skiplist_max_height, options.skiplist_p), size_bytes(0),
      immutable(false) {}

// LSM DB 생성자
// 1. 옵션 설정 및 첫 Mutable Memtable 생성
// 2. SSTable 디렉토리 생성/확인
// 3. use_existing_db=true 시 기존 SSTable 파일 로드
// 4. 다음 파일 ID와 sequence number 설정
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
}
// LSM DB Put
// 1. 엔트리 크기 계산
// 2. Mutable Memtable 공간 확보 (필요시 flush)
// 3. Skiplist에 key-value 삽입 (자동 정렬)
// 4. sequence number 증가
void LSMDB::Put(int key, const std::string& value) {
  size_t entry_bytes = EntryBytes(key, value);
  EnsureMutableCapacity(entry_bytes);
  mutable_->list.PutWithSequence(key, value, next_seq_);
  mutable_->size_bytes += entry_bytes;
  next_seq_++;
}

// LSM DB Get
// 1. Mutable Memtable 검색 (가장 최신)
// 2. Immutable Memtables 검색 (역순: 최근 생성된 것부터)
// 3. SSTable 파일 검색 (역순: 최신 파일부터)
// 4. 첫 매칭 반환 (tombstone 처리: 삭제된 경우 false)
bool LSMDB::Get(int key, std::string* out_value) const {
  std::string value;
  bool is_tombstone = false;
  if (mutable_->list.GetLatest(key, &value, &is_tombstone)) {
    if (is_tombstone) {
      return false;  // 삭제된 데이터
    }
    *out_value = value;
    return true;
  }
  
  for (int i = static_cast<int>(immutables_.size()) - 1; i >= 0; --i) {
    if (immutables_[i]->list.GetLatest(key, &value, &is_tombstone)) {
      if (is_tombstone) {
        return false;  // 삭제된 데이터
      }
      *out_value = value;
      return true;
    }
  }

  for (int i = static_cast<int>(flushed_files_.size()) - 1; i >= 0; --i) {
    if (GetFromSSTable(flushed_files_[i], key, &value, &is_tombstone)) {
      if (is_tombstone) {
        return false;  // 삭제된 데이터
      }
      *out_value = value;
      return true;
    }
  }
  return false;
}

// LSM DB Delete
// 논리적 삭제: tombstone 엔트리 삽입
// 1. 엔트리 크기 계산
// 2. Mutable Memtable 공간 확보
// 3. Skiplist에 tombstone 마커 저장
// 4. sequence number 증가 (실제 물리 삭제는 compaction 시)
void LSMDB::Delete(int key) {
  size_t entry_bytes = EntryBytes(key, "");
  EnsureMutableCapacity(entry_bytes);
  mutable_->list.DeleteWithSequence(key, next_seq_);
  mutable_->size_bytes += entry_bytes;
  next_seq_++;
}

  // LSM DB Range Scan
  // [start_key, end_key] 범위의 모든 최신 비삭제 엔트리 반환
  // 1. 모든 계층(Mutable, Immutable, SSTable)에서 범위 수집
  // 2. 키별로 최신 seq 버전만 유지 (중복 제거)
  // 3. Tombstone 제거 후 결과 반환
std::vector<std::pair<int, std::string>> LSMDB::RangeScan(int start_key,
                                                          int end_key) const {
  std::vector<std::pair<int, std::string>> out;
  std::map<int, std::pair<int64_t, std::pair<std::string, bool>>> latest_entries;
  // key -> (seq, (value, tombstone))
  auto mutable_entries = mutable_->list.RangeScanLatest(start_key, end_key);
  for (const auto& entry : mutable_entries) {
    auto it = latest_entries.find(entry.key);
    if (it == latest_entries.end() || entry.seq > it->second.first) {
      latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
    }
  }

  for (int i = static_cast<int>(immutables_.size()) - 1; i >= 0; --i) {
    auto immutable_entries = immutables_[i]->list.RangeScanLatest(start_key, end_key);
    for (const auto& entry : immutable_entries) {
      auto it = latest_entries.find(entry.key);
      if (it == latest_entries.end() || entry.seq > it->second.first) {
        latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
      }
    }
  }

  for (int i = static_cast<int>(flushed_files_.size()) - 1; i >= 0; --i) {
    auto sstable_entries = RangeScanSSTable(flushed_files_[i], start_key, end_key);
    for (const auto& entry : sstable_entries) {
      auto it = latest_entries.find(entry.key);
      if (it == latest_entries.end() || entry.seq > it->second.first) {
        latest_entries[entry.key] = {entry.seq, {entry.value, entry.tombstone}};
      }
    }
  }

  for (const auto& pair : latest_entries) {
    if (!pair.second.second.second) {  // !tombstone
      out.push_back({pair.first, pair.second.second.first});
    }
  }
  return out;
}

// Immutable Memtable 개수 반환
size_t LSMDB::ImmutableCount() const { return immutables_.size(); }

// Flushed SSTable 파일 개수 반환
size_t LSMDB::FlushedFileCount() const { return flushed_files_.size(); }

// Mutable Memtable 현재 크기(바이트) 반환
size_t LSMDB::MutableSizeBytes() const { return mutable_->size_bytes; }

// Mutable Memtable 공간 확보 함수
// 주어진 엔트리를 수용할 공간이 부족하면 Mutable을 Immutable로 변환
// 1. 크기 확인: 기존 크기 + 새 엔트리 <= 최대값이면 그냥 반환
// 2. 용량 초과: Mutable을 Immutable로 변환
// 3. 새 Mutable 생성
// 4. 모든 Immutable 즉시 flush
void LSMDB::EnsureMutableCapacity(size_t entry_bytes) {
  if (mutable_->size_bytes + entry_bytes <= options_.max_memtable_bytes) {
    return;
  }

  mutable_->immutable = true;
  immutables_.push_back(std::move(mutable_));
  mutable_ = std::make_unique<MemTable>(options_);

  FlushAllImmutables();
}

// 엔트리 크기 계산 함수
size_t LSMDB::EntryBytes(int key, const std::string& value) const {
  return sizeof(key) + value.size();
}

// 모든 Immutable Memtable flush 함수
// Immutable 리스트가 모두 소진될 때까지 하나씩 flush
void LSMDB::FlushAllImmutables() {
  while (!immutables_.empty()) {
    FlushOneImmutable(immutables_.front().get());
    immutables_.erase(immutables_.begin());
  }
}

  // 하나의 Immutable Memtable을 SSTable로 flush하는 함수
  // 1. 유일한 파일 ID 할당
  // 2. Immutable의 전체 범위 스캔으로 모든 엔트리 추출
  // 3. SSTable 파일 생성 (Bloom filter 옵션 반영)
  // 4. 생성된 파일을 flushed_files_에 추가
  // 5. Compaction 판단 및 필요시 실행
void LSMDB::FlushOneImmutable(const MemTable* table) {
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

// SSTable Compaction 판단 및 실행 함수
// 1. Compaction 활성화 확인
// 2. SSTable 파일 개수 > 임계값이면 모든 파일 병합
// 3. 병합 결과로 기존 파일들 교체
// 4. 저장소 최적화 및 읽기 성능 향상 (LSM 구조 유지)
void LSMDB::MaybeCompactSSTables() {
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
