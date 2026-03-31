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
  struct RangeEntry { // memdb에서 range scan을 할때 필요할 수도 있는 구조체
    int key;
    std::string value;
    bool tombstone;
  };

  explicit SkipList(int max_level = 16, float p = 0.5f);
  ~SkipList();

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;

  void Put(int key, const std::string& value);
  bool Get(int key, std::string* out_value) const;
  bool Delete(int key);
  std::vector<std::pair<int, std::string>>
  RangeScan(int start_key,
            int end_key) const; // skiplist 내부 range scan와 memdb range
                                // scan의 차이를 고려하여 설계
private:
  struct Node {
    int key;
    int64_t seq; // sequence number
    std::string value;
    bool tombstone;
    Node* next;
    Node* down; // 필요시 추가 노드 포인터 선언하여 사용 가능
  };

  int RandomLevel();
  Node* FindGreaterOrEqual(int key, int64_t seq,
                           std::vector<Node*>* update) const;
  static bool Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq);

  Node* head_;
  int max_level_;
  float p_;
  int64_t next_seq_;
};

#endif // LAB2_SKIPLIST_H
