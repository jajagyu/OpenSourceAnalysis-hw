#include "skiplist.h"

#include <algorithm>
#include <limits>
#include <random>

SkipList::SkipList(int max_level, float p)
    : head_(nullptr), max_level_(std::max(1, max_level)), p_(p), next_seq_(1) {
  // Build a tower of sentinel heads.
  head_ = new Node{0, 0, std::string(), false, nullptr, nullptr};
  Node* curr = head_;
  for (int i = 1; i < max_level_; ++i) {
    Node* above = new Node{0, 0, std::string(), false, nullptr, curr};
    curr = above;
  }
  head_ = curr; // Top-level head.
}

SkipList::~SkipList() {
  // Delete all nodes level by level from the top.
  Node* level = head_;
  while (level) {
    Node* next_level = level->down;
    Node* node = level;
    while (node) {
      Node* next = node->next;
      delete node;
      node = next;
    }
    level = next_level;
  }
}

int SkipList::RandomLevel() {
  thread_local std::mt19937 rng(std::random_device{}());
  std::bernoulli_distribution coin(p_);
  int level = 1;
  while (level < max_level_ && coin(rng)) {
    ++level;
  }
  return level;
}

bool SkipList::Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq) {
  if (a_key != b_key) {
    return a_key < b_key;
  }
  return a_seq > b_seq; // Descending sequence for same key.
}

SkipList::Node* SkipList::FindGreaterOrEqual(int key, int64_t seq,
                                             std::vector<Node*>* update) const {
  Node* x = head_;
  int level = max_level_ - 1;
  if (update) {
    update->assign(max_level_, nullptr);
  }

  while (x) {
    while (x->next && Less(x->next->key, x->next->seq, key, seq)) {
      x = x->next;
    }
    if (update && level >= 0) {
      (*update)[level] = x;
    }
    if (x->down) {
      x = x->down;
      --level;
    } else {
      break;
    }
  }
  return x->next;
}

void SkipList::Put(int key, const std::string& value) {
  PutWithSequence(key, value, next_seq_++);
}

void SkipList::PutWithSequence(int key, const std::string& value, int64_t seq) {
  std::vector<Node*> update;
  FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), &update);

  int node_level = RandomLevel();

  Node* down = nullptr;
  for (int lvl = 0; lvl < node_level; ++lvl) {
    Node* prev = update[lvl];
    Node* n = new Node{key,   seq,     (lvl == 0 ? value : std::string()),
                       false, nullptr, down};
    n->next = prev->next;
    prev->next = n;
    down = n;
  }
}

bool SkipList::Get(int key, std::string* out_value) const {
  bool tombstone = false;
  if (!GetLatest(key, out_value, &tombstone)) {
    return false;
  }
  return !tombstone;
}

bool SkipList::GetLatest(int key, std::string* out_value,
                         bool* is_tombstone) const {
  Node* node =
      FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), nullptr);
  if (node && node->key == key) {
    if (out_value) {
      *out_value = node->value;
    }
    if (is_tombstone) {
      *is_tombstone = node->tombstone;
    }
    return true;
  }
  return false;
}

bool SkipList::Delete(int key) {
  return DeleteWithSequence(key, next_seq_++);
}

bool SkipList::DeleteWithSequence(int key, int64_t seq) {
  std::vector<Node*> update;
  Node* node =
      FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), &update);
  (void)node;

  int node_level = RandomLevel();
  Node* down = nullptr;
  for (int lvl = 0; lvl < node_level; ++lvl) {
    Node* prev = update[lvl];
    Node* n = new Node{key, seq, std::string(), true, nullptr, down};
    n->next = prev->next;
    prev->next = n;
    down = n;
  }
  return true;
}

std::vector<std::pair<int, std::string>>
SkipList::RangeScan(int start_key, int end_key) const {
  std::vector<std::pair<int, std::string>> out;
  if (start_key > end_key) {
    return out;
  }
  Node* node = FindGreaterOrEqual(start_key,
                                  std::numeric_limits<int64_t>::max(), nullptr);
  while (node && node->key <= end_key) {
    int current_key = node->key;
    if (!node->tombstone) {
      out.emplace_back(node->key, node->value);
    }
    while (node && node->key == current_key) {
      node = node->next;
    }
  }
  return out;
}

std::vector<SkipList::RangeEntry> SkipList::RangeScanLatest(int start_key,
                                                            int end_key) const {
  std::vector<RangeEntry> out;
  if (start_key > end_key) {
    return out;
  }
  Node* node = FindGreaterOrEqual(start_key,
                                  std::numeric_limits<int64_t>::max(), nullptr);
  while (node && node->key <= end_key) {
    int current_key = node->key;
    out.push_back(RangeEntry{node->key, node->seq, node->value,
                             node->tombstone});
    while (node && node->key == current_key) {
      node = node->next;
    }
  }
  return out;
}
