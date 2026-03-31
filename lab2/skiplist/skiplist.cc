#include "skiplist.h"

#include <algorithm>
#include <limits>
#include <map>
#include <random>

// SkipList Constructor. head node, level에 따른 초기 설정 필요
SkipList::SkipList(int max_level, float p)
    : head_(nullptr), max_level_(std::max(1, max_level)), p_(p), next_seq_(1) {
  head_ = new Node();
  head_->key = std::numeric_limits<int>::min();
  head_->seq = 0;
  head_->value = "";
  head_->tombstone = false;
  head_->next = nullptr;
  head_->down = nullptr;
  
  Node* curr = head_;
  for (int i = 1; i < max_level_; ++i) {
    Node* next_node = new Node();
    next_node->key = std::numeric_limits<int>::min();
    next_node->seq = 0;
    next_node->value = "";
    next_node->tombstone = false;
    next_node->next = nullptr;
    next_node->down = curr;
    curr = next_node;
  }
  head_ = curr;
}

// SkipList Destructor. 생성한 노드에 대해 모두 delete
SkipList::~SkipList() {
  Node* curr = head_;
  while (curr != nullptr) {
    Node* down = curr->down;
    Node* next = curr->next;
    while (next != nullptr) {
      Node* temp = next;
      next = next->next;
      delete temp;
    }
    delete curr;
    curr = down;
  }
}

// SkipList Put operation시 높이 설정 함수
int SkipList::RandomLevel() {
  std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<> dis(0.0, 1.0);
  
  int level = 1;
  while (dis(gen) < p_ && level < max_level_) {
    level++;
  }
  return level;
}

// SkipList에 새로운 key 및 value를 삽입하는 Put 함수
// sequence number 필요
void SkipList::Put(int key, const std::string& value) {
  std::vector<Node*> update(max_level_, nullptr);
  FindGreaterOrEqual(key, 0, &update);
  
  // Always insert new node with new sequence number (out-of-place update)
  int new_level = RandomLevel();
  if (new_level > max_level_) {
    new_level = max_level_;
  }
  
  Node* new_node = new Node();
  new_node->key = key;
  new_node->seq = next_seq_;
  new_node->value = value;
  new_node->tombstone = false;
  new_node->next = nullptr;
  new_node->down = nullptr;
  
  Node* curr = new_node;
  for (int i = 0; i < new_level; ++i) {
    if (update[i] != nullptr) {
      curr->next = update[i]->next;
      update[i]->next = curr;
    }
    
    if (i < new_level - 1) {
      Node* next_level_node = new Node();
      next_level_node->key = key;
      next_level_node->seq = next_seq_;
      next_level_node->value = value;
      next_level_node->tombstone = false;
      next_level_node->next = nullptr;
      next_level_node->down = curr;
      curr = next_level_node;
    }
  }
  
  next_seq_++;
}

// SkipList에 서 key에 해당하는 value 찾기. 존재하면 true, 없으면 (tombstone
// 고려) false 반환. value는 out_value에 저장
bool SkipList::Get(int key, std::string* out_value) const {
  RangeEntry latest;
  if (!GetLatestEntry(key, &latest)) {
    return false;
  }

  if (latest.tombstone) {
    return false;
  }

  *out_value = latest.value;
  return true;
}

bool SkipList::GetLatestEntry(int key, RangeEntry* out_entry) const {
  Node* x = FindGreaterOrEqual(key, 0, nullptr);
  if (x == nullptr || x->key != key) {
    return false;
  }

  Node* latest = x;
  while (x != nullptr && x->key == key) {
    if (x->seq > latest->seq) {
      latest = x;
    }
    x = x->next;
  }

  if (out_entry != nullptr) {
    out_entry->key = latest->key;
    out_entry->value = latest->value;
    out_entry->tombstone = latest->tombstone;
  }
  return true;
}

// SkipList Delete operation. Tombstone으로 삭제 진행
bool SkipList::Delete(int key) {
  RangeEntry latest;
  bool existed_non_tombstone =
      GetLatestEntry(key, &latest) && !latest.tombstone;

  std::vector<Node*> update(max_level_, nullptr);
  FindGreaterOrEqual(key, 0, &update);

  int new_level = RandomLevel();
  if (new_level > max_level_) {
    new_level = max_level_;
  }

  Node* down = nullptr;
  for (int i = 0; i < new_level; ++i) {
    Node* node = new Node();
    node->key = key;
    node->seq = next_seq_;
    node->value = "";
    node->tombstone = true;
    node->down = down;
    node->next = update[i]->next;
    update[i]->next = node;
    down = node;
  }

  ++next_seq_;
  return existed_non_tombstone;
}

// SkipList range scan operation. 해당하는 노드를 vector에 모아 반환
std::vector<std::pair<int, std::string>>
SkipList::RangeScan(int start_key, int end_key) const {
  std::vector<std::pair<int, std::string>> out;
  std::vector<RangeEntry> entries = RangeScanEntries(start_key, end_key);
  for (const auto& e : entries) {
    if (!e.tombstone) {
      out.push_back({e.key, e.value});
    }
  }
  return out;
}

std::vector<SkipList::RangeEntry>
SkipList::RangeScanEntries(int start_key, int end_key) const {
  std::vector<RangeEntry> out;

  if (start_key > end_key) {
    return out;
  }

  Node* x = FindGreaterOrEqual(start_key, 0, nullptr);

  std::map<int, std::pair<int64_t, RangeEntry>> result_map;
  while (x != nullptr && x->key <= end_key) {
    if (x->key >= start_key) {
      auto it = result_map.find(x->key);
      if (it == result_map.end() || x->seq > it->second.first) {
        RangeEntry entry;
        entry.key = x->key;
        entry.value = x->value;
        entry.tombstone = x->tombstone;
        result_map[x->key] = {x->seq, entry};
      }
    }
    x = x->next;
  }

  for (const auto& pair : result_map) {
    out.push_back(pair.second.second);
  }

  return out;
}

SkipList::Node* SkipList::FindGreaterOrEqual(int key, int64_t seq,
                                              std::vector<Node*>* update) const {
  Node* x = head_;
  int level = max_level_ - 1;
  
  while (x != nullptr) {
    Node* next = x->next;
    while (next != nullptr && Less(next->key, next->seq, key, seq)) {
      x = next;
      next = x->next;
    }
    
    if (update != nullptr && level >= 0 &&
        static_cast<size_t>(level) < update->size()) {
      (*update)[level] = x;
    }

    if (x->down == nullptr) {
      return x->next;
    }

    x = x->down;
    --level;
  }
  
  return nullptr;
}

bool SkipList::Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq) {
  if (a_key != b_key) {
    return a_key < b_key;
  }
  return a_seq < b_seq;
}
