/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
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

// Memtable, Skiplist, SSTable, Bloom filter, Compaction 동작 제어

struct MemDBOptions {
  size_t max_memtable_bytes = 4 * 1024 * 1024;       // Memtable 최대 크기 (도달 시 flush)
  float skiplist_p = 0.5f;                            // Skiplist 승격 확률
  int skiplist_max_height = 16;                       // Skiplist 최대 높이
  std::string sst_dir = "sst";                        // SSTable 저장 디렉토리
  bool use_existing_db = false;                       // 기존 DB 재사용 여부
  bool enable_sstable_bloom_filter = false;           // SSTable Bloom filter 활성화
  size_t sstable_bloom_bits_per_key = 10;             // Bloom filter: 키당 비트 수
  size_t sstable_bloom_hash_count = 6;                // Bloom filter: 해시 함수 개수
  bool enable_compaction = true;                      // Compaction 활성화
  size_t compaction_file_threshold = 2;               // Compaction 발동 SSTable 개수 임계값
};

// LSM 트리 기반 Key-Value 저장소
// 계층: Mutable Memtable -> Immutable Memtable -> SSTable 파일
// 쓰기 최적화: 메모리 기반 쓰기 후 배경 flush/compaction으로 성능 향상
class LSMDB {
 public:
  explicit LSMDB(const MemDBOptions& options);

  // DB Put: key-value 쌍 삽입 및 업데이트
  // 1. Mutable Memtable에 우선 저장
  // 2. 크기 초과 시 새 Mutable 생성 및 기존 Immutable로 변환
  // 3. Immutable 자동 flush로 SSTable 파일 생성
  void Put(int key, const std::string& value);

  // DB Get: 키로 값 조회
  // 검색 순서: Mutable -> Immutable (역순) -> SSTable (역순)
  // 최신 데이터부터 탐색하여 첫 매칭 반환 (tombstone 처리)
  bool Get(int key, std::string* out_value) const;

    // DB Delete: 키 삭제 (논리적 삭제)
    // Tombstone 엔트리를 새 seq로 삽입 (실제 물리 삭제는 compaction 시)
    // 1. Mutable Memtable에 tombstone 저장
    // 2. 크기 초과 시 flush 진행
  void Delete(int key);

    // DB Range Scan: 키 범위 조회
    // [start_key, end_key] 범위의 모든 최신 비삭제 엔트리 반환
    // 1. 모든 계층(Mutable, Immutable, SSTable)에서 범위 수집
    // 2. 키별로 최신 seq 버전 유지
    // 3. Tombstone 제거 후 반환
  std::vector<std::pair<int, std::string>> RangeScan(int start_key,
                                                     int end_key) const;

  
  size_t ImmutableCount() const;
  size_t FlushedFileCount() const;
  size_t MutableSizeBytes() const;

 private:
    // Memtable 내부 구조
    // Skiplist 기반 정렬 저장소 + 크기 추적
  struct MemTable {
    explicit MemTable(const MemDBOptions& options);

    SkipList list;
    size_t size_bytes;
    bool immutable;  // Immutable 플래그
  };

  // Mutable Memtable 공간 확보 함수
  // 필요 시 현재 Mutable을 Immutable로 변환하고 새 Mutable 생성
  void EnsureMutableCapacity(size_t entry_bytes);

  // 엔트리 크기 계산 (메타데이터 포함)
  size_t EntryBytes(int key, const std::string& value) const;

  // 모든 Immutable Memtable을 SSTable로 flush
  // Immutable 리스트가 빌 때까지 반복 flush
  void FlushAllImmutables();

  // 하나의 Immutable Memtable을 SSTable 파일로 flush
  // 1. 유일한 파일 ID 할당
  // 2. Immutable 내 모든 엔트리 추출
  // 3. SSTable 파일 작성
  // 4. Compaction 판단 및 필요시 실행
  void FlushOneImmutable(const MemTable* table);

  // SSTable 파일 개수 확인 및 Compaction 판단 함수
  // Compaction 활성화 && 파일 개수 > 임계값 시 모든 파일 병합
  void MaybeCompactSSTables();

  MemDBOptions options_;
  std::unique_ptr<MemTable> mutable_;
  std::vector<std::unique_ptr<MemTable>> immutables_;
  std::vector<SSTableFile> flushed_files_;
  uint64_t next_file_id_;
  int64_t next_seq_;
};

#endif  // LAB3_MEMDB_H
