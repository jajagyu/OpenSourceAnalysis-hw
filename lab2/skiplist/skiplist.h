#ifndef LAB2_SKIPLIST_H
#define LAB2_SKIPLIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 필요시 내부 function, 변수 등 선언 가능

class SkipList {
public:
  // RangeScan 시 key의 최신 상태(값/삭제 여부)를 함께 전달하기 위한 엔트리.
  // MemDB 계층에서는 tombstone 여부까지 알아야 오래된 값을 잘못 되살리지 않음.
  struct RangeEntry {
    int key;
    std::string value;
    bool tombstone;
  };

  explicit SkipList(int max_level = 16, float p = 0.5f);
  ~SkipList();

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;

  // Out-of-place update: 같은 key라도 새 seq를 가진 새 노드를 삽입
  void Put(int key, const std::string& value);

  // key의 최신 버전이 tombstone이 아니면 value를 반환
  bool Get(int key, std::string* out_value) const;

  // key의 최신 엔트리(값 또는 tombstone)를 그대로 반환
  // 상위 계층(MemDB)이 tombstone 마스킹을 정확히 처리할 때 사용
  bool GetLatestEntry(int key, RangeEntry* out_entry) const;

  // 삭제는 실제 물리 삭제가 아니라 tombstone 엔트리를 새 seq로 기록하는 방식
  bool Delete(int key);

  // [start_key, end_key] 범위의 최신 비삭제 값만 반환
  std::vector<std::pair<int, std::string>>
  RangeScan(int start_key,
            int end_key) const;

  // [start_key, end_key] 범위의 최신 엔트리를 tombstone 포함으로 반환한다.
  // MemDB에서 memtable 간 병합 시 "최신 tombstone 우선" 처리를 위해 사용
  std::vector<RangeEntry> RangeScanEntries(int start_key, int end_key) const;

private:
  struct Node {
    int key;
    // 동일 key에 대한 버전 구분용 sequence number (클수록 최신)
    int64_t seq;
    std::string value;
    // true면 삭제 마커(tombstone) 엔트리
    bool tombstone;
    // 같은 레벨의 다음 노드
    Node* next;
    // 한 단계 아래 레벨의 같은 (key, seq) 노드
    Node* down;
  };

  // p 확률 기반으로 삽입 레벨(높이) 결정.
  int RandomLevel();

  // (key, seq) 이상이 처음 나오는 bottom-level 노드를 찾음
  // update가 주어지면 각 레벨에서 직전 노드를 채움
  Node* FindGreaterOrEqual(int key, int64_t seq,
                           std::vector<Node*>* update) const;

  // (key, seq) 쌍의 정렬 비교: key 오름차순, key가 같으면 seq 오름차순
  static bool Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq);

  // 최상위 헤드 노드
  Node* head_;
  // 최대 레벨 수
  int max_level_;
  // RandomLevel에서 사용하는 승격 확률
  float p_;
  // 다음에 부여할 sequence number
  int64_t next_seq_;
};

#endif // LAB2_SKIPLIST_H
