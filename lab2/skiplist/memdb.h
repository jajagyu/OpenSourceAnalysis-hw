#ifndef LAB2_MEMDB_H
#define LAB2_MEMDB_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "skiplist.h"

struct MemDBOptions {
  // mutable memtable이 이 크기를 넘기면 immutable로 변환
  size_t max_memtable_bytes = 4 * 1024 * 1024;
  // skiplist 승격 확률
  float skiplist_p = 0.5f;
  // skiplist 최대 높이
  int skiplist_max_height = 16;
};

class InMemoryDB {
 public:
  explicit InMemoryDB(const MemDBOptions& options);

  void Put(int key, const std::string& value);

  // 최신 tombstone을 포함하여 조회
  bool Get(int key, std::string* out_value) const;
  // 삭제는 tombstone 기록으로 처리
  void Delete(int key);
  // 모든 memtable을 최신 우선으로 병합해 범위 조회
  std::vector<std::pair<int, std::string>> RangeScan(int start_key, int end_key) const;

  size_t ImmutableCount() const;
  size_t MutableSizeBytes() const;

 private:
  struct MemTable {
    explicit MemTable(const MemDBOptions& options);

    // 하나의 skiplist가 하나의 memtable 데이터 컨테이너 역할을 함
    SkipList list;

    // memtable이 차지하는 총 바이트 수 (key+value 크기 합)
    size_t size_bytes;
    // true면 더 이상 쓰기 대상이 아닌 immutable 상태
    bool immutable;
  };

  // 새 엔트리 기록 전, mutable 용량이 충분한지 확인하고 필요 시 immutable로 변환
  void EnsureMutableCapacity(size_t entry_bytes);
  // key+value의 단순 엔트리 크기 계산
  size_t EntryBytes(int key, const std::string& value) const;

  MemDBOptions options_;
  // 현재 쓰기 대상 memtable
  std::unique_ptr<MemTable> mutable_;
  // 오래된 memtable들(뒤로 갈수록 최신이 아님)
  std::vector<std::unique_ptr<MemTable>> immutables_;
};

#endif  // LAB2_MEMDB_H
