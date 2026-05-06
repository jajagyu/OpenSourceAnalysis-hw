#ifndef LAB2_SKIPLIST_H
#define LAB2_SKIPLIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class SkipList {
 public:
  struct RangeEntry {
    int key;
    int64_t seq;
    std::string value;
    bool tombstone;
  };

  explicit SkipList(int max_level = 16, float p = 0.5f);
  ~SkipList();

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;

  void Put(int key, const std::string& value);
  void PutWithSequence(int key, const std::string& value, int64_t seq);
  bool Get(int key, std::string* out_value) const;
  bool GetLatest(int key, std::string* out_value, bool* is_tombstone) const;
  bool Delete(int key);
  bool DeleteWithSequence(int key, int64_t seq);
  std::vector<std::pair<int, std::string>> RangeScan(int start_key, int end_key) const;
  std::vector<RangeEntry> RangeScanLatest(int start_key, int end_key) const;

 private:
  struct Node {
    int key;
    int64_t seq;
    std::string value;
    bool tombstone;
    Node* next;
    Node* down;
  };

  int RandomLevel();
  Node* FindGreaterOrEqual(int key, int64_t seq, std::vector<Node*>* update) const;
  static bool Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq);

  Node* head_;
  int max_level_;
  float p_;
  int64_t next_seq_;
};

#endif  // LAB2_SKIPLIST_H
