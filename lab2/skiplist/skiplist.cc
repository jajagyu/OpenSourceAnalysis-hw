#include "skiplist.h"

#include <algorithm>
#include <limits>
#include <map>
#include <random>

// SkipList Constructor. head node, level에 따른 초기 설정 필요
SkipList::SkipList(int max_level, float p)
    : head_(nullptr), max_level_(std::max(1, max_level)), p_(p), next_seq_(1) {
  // 먼저 bottom head를 만든 뒤, 위로 쌓아 최종적으로 top head를 head_로 둔다.
  head_ = new Node();
  head_->key = std::numeric_limits<int>::min(); //int 최솟값을 key로 사용해 항상 가장 작은 key보다 앞에 오도록 함
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
  // curr는 가장 위 레벨 head가 되며 이를 head_로 설정
  head_ = curr;
}

// SkipList Destructor. 생성한 노드에 대해 모두 delete
SkipList::~SkipList() {
  // 레벨을 위에서 아래로 내려가며, 각 레벨의 수평 리스트를 모두 해제
  // down 체인을 통해 레벨을 바꾸고, next 체인을 통해 같은 레벨 노드를 순회
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
  // level=1이 기본이며, p_ 확률로 상위 레벨로 계속 승격
  std::mt19937 gen(std::random_device{}()); //랜덤 시드 생성
  std::uniform_real_distribution<> dis(0.0, 1.0); //0에서 1 사이의 실수로 분포 생성시키는 분포 객체 생성
  
  int level = 1;
  while (dis(gen) < p_ && level < max_level_) { // p_ 확률로 레벨을 하나씩 올린다. max_level_ 초과는 방지
    level++;
  }
  return level;
}

// SkipList에 새로운 key 및 value를 삽입하는 Put 함수
// sequence number 필요
void SkipList::Put(int key, const std::string& value) {
  std::vector<Node*> update(max_level_, nullptr);
  // 각 레벨의 삽입 직전 노드를 확보 (새 노드가 이 노드 뒤에 삽입될 것임)
  FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), &update);
  
  // 새 노드의 레벨을 랜덤하게 결정
  int new_level = RandomLevel();

  

  Node* new_node = new Node();
  new_node->key = key;
  new_node->seq = next_seq_;
  // 실제 값은 최하위(bottom) 레벨 노드에만 저장
  new_node->value = value;
  new_node->tombstone = false;
  new_node->next = nullptr;
  new_node->down = nullptr;
  
  Node* curr = new_node;
  for (int i = 0; i < new_level; ++i) {
    // 해당 레벨의 predecessor 뒤에 새 노드를 연결
    if (update[i] != nullptr) {
      curr->next = update[i]->next;
      update[i]->next = curr;
    }
    
    // 상위 레벨 노드는 down으로 바로 아래 레벨 노드와 수직 연결
    if (i < new_level - 1) {
      Node* next_level_node = new Node();
      next_level_node->key = key;
      next_level_node->seq = next_seq_;
      // 상위 인덱스 레벨은 탐색 가속용이므로 value는 비워 둠
      next_level_node->value = "";
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
  // 최신 엔트리를 먼저 찾고, tombstone 여부를 최종 판단
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
  // (key, INT64_MAX) 이상의 첫 노드는 해당 key의 최신 버전(최대 seq)
  Node* x = FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), nullptr);
  if (x == nullptr || x->key != key) {
    return false;
  }

  if (out_entry != nullptr) {
    out_entry->key = x->key;
    out_entry->value = x->value;
    out_entry->tombstone = x->tombstone;
  }
  return true;
}

// SkipList Delete operation. Tombstone으로 삭제 진행
bool SkipList::Delete(int key) {
  // 기존 노드를 지우지 않고, tombstone 새 버전을 기록
  std::vector<Node*> update(max_level_, nullptr);
  FindGreaterOrEqual(key, std::numeric_limits<int64_t>::max(), &update);

  int new_level = RandomLevel();
  if (new_level > max_level_) {
    new_level = max_level_;
  }

  Node* down = nullptr;
  for (int i = 0; i < new_level; ++i) {
    Node* node = new Node();
    if (node == nullptr) {
      return false; // 메모리 할당 실패
    }
    node->key = key;
    node->seq = next_seq_;
    node->value = "";
    node->tombstone = true;
    node->down = down;
    node->next = update[i]->next;
    update[i]->next = node;
    // 위 레벨에서 아래 레벨로 수직 연결.
    down = node;
  }

  ++next_seq_;
  return true; // tombstone 기록 성공
}

// SkipList range scan operation. 해당하는 노드를 vector에 모아 반환
std::vector<std::pair<int, std::string>>
SkipList::RangeScan(int start_key, int end_key) const {
  // 내부적으로 tombstone 포함 결과를 받은 뒤, 삭제되지 않은 값만 반환
  std::vector<std::pair<int, std::string>> out;
  std::vector<RangeEntry> entries = RangeScanEntries(start_key, end_key);
  for (const auto& e : entries) {
    if (!e.tombstone) {
      out.push_back({e.key, e.value});
    }
  }
  return out;
}

// 범위에 해당하는 노드를 vector에 모아 반환(tombstone 포함)
std::vector<SkipList::RangeEntry>
SkipList::RangeScanEntries(int start_key, int end_key) const {
  std::vector<RangeEntry> out;

  if (start_key > end_key) {
    return out;
  }

  // start_key 이상 첫 노드부터 선형 순회
  // key별로 첫 나타남(최신 seq)만 유지
  Node* x = FindGreaterOrEqual(start_key, std::numeric_limits<int64_t>::max(), nullptr);

  std::map<int, RangeEntry> result_map;
  while (x != nullptr && x->key <= end_key) {
    if (x->key >= start_key) {
      if (result_map.find(x->key) == result_map.end()) {  // 각 key의 첫 노드만 저장 (최신)
        RangeEntry entry;
        entry.key = x->key;
        entry.value = x->value;
        entry.tombstone = x->tombstone;
        result_map[x->key] = entry;
      }
    }
    x = x->next;
  }

  for (const auto& pair : result_map) {
    out.push_back(pair.second);
  }

  return out;
}

// (key, seq) 이상이 처음 나오는 bottom-level 노드를 찾음 (seq 내림차순이므로 최신이 먼저)
// update가 주어지면 각 레벨에서 바로 앞 노드를 채움
SkipList::Node* SkipList::FindGreaterOrEqual(int key, int64_t seq,
                                              std::vector<Node*>* update) const {
  Node* x = head_;
  int level = max_level_ - 1;
  
  // 각 레벨에서 목표보다 작은 마지막 노드를 찾고 아래로 내려감
  while (x != nullptr) {
    Node* next = x->next;
    while (next != nullptr && Less(next->key, next->seq, key, seq)) {
      x = next;
      next = x->next;
    }
    
    if (update != nullptr && level >= 0 &&
        static_cast<size_t>(level) < update->size()) {
      // 이 레벨에서의 predecessor 저장
      (*update)[level] = x;
    }

    if (x->down == nullptr) {
      // bottom 레벨에서 첫 번째 >= (key, seq) 노드
      return x->next;
    }

    x = x->down;
    --level;
  }
  
  return nullptr;
}


bool SkipList::Less(int a_key, int64_t a_seq, int b_key, int64_t b_seq) {
  // key 우선 오름차순, 동일 key면 seq 내림차순 (최신이 먼저)
  if (a_key != b_key) {
    return a_key < b_key;
  }
  return a_seq > b_seq;
}