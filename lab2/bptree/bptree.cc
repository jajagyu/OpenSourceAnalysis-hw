#include "bptree.h"

#include <algorithm>

// B+Tree Constructor. degree 설정 등 최초 설정 진행
BPlusTree::BPlusTree(int degree) : root_(nullptr), degree_(degree) {
  // 너무 작은 degree는 분기 의미가 약해지므로 최소 3으로 보정.
  if (degree_ < 3) {
    degree_ = 3;
  }
  // 초기에는 비어 있는 단일 leaf 루트로 시작한다.
  root_ = new Node(true);
}

// B+Tree Destructor. B+Tree에 존재하는 모든 Node delete 필요
BPlusTree::~BPlusTree() {
  // 루트부터 재귀 해제.
  Destroy(root_);
  root_ = nullptr;
}

// B+Tree Put operation
void BPlusTree::Put(int key, const std::string& value) {
  // 하향 삽입 결과를 받고, 루트 split이면 새 루트를 만든다.
  SplitResult res = Insert(root_, key, value);
  if (!res.split) {
    return;
  }

  // old root, new right를 자식으로 갖는 internal root 생성.
  Node* new_root = new Node(false);
  new_root->children.push_back(root_);
  new_root->children.push_back(res.right);
  new_root->keys.push_back(res.promoted_key);
  root_ = new_root;
}

// B+Tree Get operation
bool BPlusTree::Get(int key, std::string* value) const {
  // key가 속할 leaf를 찾고, leaf 내부에서 이진 탐색.
  Node* leaf = FindLeaf(key);
  if (leaf == nullptr) {
    return false;
  }

  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
  if (it == leaf->keys.end() || *it != key) {
    return false;
  }

  size_t pos = static_cast<size_t>(it - leaf->keys.begin());
  *value = leaf->values[pos];
  return true;
}

// B+Tree Range Scan operation
std::vector<std::pair<int, std::string>>
BPlusTree::RangeScan(int start_key, int end_key) const {
  std::vector<std::pair<int, std::string>> out;

  // 역방향/빈 범위 방어.
  if (start_key > end_key) {
    return out;
  }

  // 시작 key가 속한 leaf부터 오른쪽 leaf 체인을 순회한다.
  Node* leaf = FindLeaf(start_key);
  while (leaf != nullptr) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      int key = leaf->keys[i];
      if (key < start_key) {
        continue;
      }
      if (key > end_key) {
        // leaf가 정렬되어 있으므로 end_key 초과 시 즉시 종료 가능.
        return out;
      }
      out.push_back({key, leaf->values[i]});
    }
    leaf = leaf->next;
  }

  return out;
}

// B+Tree Delete operation. In-place update로 진행 됨으로, 실제 노드 삭제가
// 진행되야함
bool BPlusTree::Delete(int key) {
  // 실제 엔트리 삭제를 수행한다 (tombstone 없음).
  bool deleted = false;
  DeleteFromNode(root_, key, &deleted);

  // 루트가 internal인데 자식이 비면 새 빈 leaf 루트로 초기화.
  if (!root_->is_leaf && root_->children.empty()) {
    delete root_;
    root_ = new Node(true);
    return deleted;
  }

  // 루트 축소: internal root의 자식이 하나만 남으면 높이를 줄인다.
  if (!root_->is_leaf && root_->children.size() == 1) {
    Node* old_root = root_;
    root_ = root_->children[0];
    old_root->children.clear();
    delete old_root;
  }

  return deleted;
}

void BPlusTree::Destroy(Node* node) {
  if (node == nullptr) {
    return;
  }

  // internal 노드는 자식을 먼저 재귀적으로 해제한다.
  if (!node->is_leaf) {
    for (Node* child : node->children) {
      Destroy(child);
    }
  }
  delete node;
}

BPlusTree::SplitResult BPlusTree::Insert(Node* node, int key,
                                         const std::string& value) {
  // 한 노드가 가질 수 있는 최대 key 수.
  const int max_keys = degree_ - 1;

  if (node->is_leaf) {
    // leaf에서 key 위치를 이진 탐색.
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    size_t pos = static_cast<size_t>(it - node->keys.begin());

    // in-place update: 기존 key면 값만 갱신.
    if (it != node->keys.end() && *it == key) {
      node->values[pos] = value;
      return {false, 0, nullptr};
    }

    // 새 key/value 삽입.
    node->keys.insert(it, key);
    node->values.insert(node->values.begin() + static_cast<long long>(pos),
                        value);

    // overflow가 아니면 종료.
    if (static_cast<int>(node->keys.size()) <= max_keys) {
      return {false, 0, nullptr};
    }

    // leaf split: 중간 기준으로 오른쪽 leaf를 분리한다.
    int mid = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->values.assign(node->values.begin() + mid, node->values.end());

    node->keys.erase(node->keys.begin() + mid, node->keys.end());
    node->values.erase(node->values.begin() + mid, node->values.end());

    // leaf 연결 리스트 유지.
    right->next = node->next;
    node->next = right;

    // B+Tree leaf split에서 부모로 올리는 키는 right의 최소 key.
    return {true, right->keys.front(), right};
  }

  // internal: key가 들어갈 자식으로 재귀 삽입.
  int idx = ChildIndex(node, key);
  SplitResult child_res = Insert(node->children[idx], key, value);
  if (!child_res.split) {
    // 자식 변경으로 separator가 달라질 수 있으므로 재계산.
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

  // 자식 split 반영: 승격 키와 오른쪽 자식을 삽입.
  node->keys.insert(node->keys.begin() + idx, child_res.promoted_key);
  node->children.insert(node->children.begin() + idx + 1, child_res.right);

  if (static_cast<int>(node->keys.size()) <= max_keys) {
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

  // internal split: mid key는 부모로 승격되고, 양쪽 노드에서 제거된다.
  int mid = static_cast<int>(node->keys.size()) / 2;
  int promoted = node->keys[mid];
  Node* right = new Node(false);

  right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
  right->children.assign(node->children.begin() + mid + 1, node->children.end());

  node->keys.erase(node->keys.begin() + mid, node->keys.end());
  node->children.erase(node->children.begin() + mid + 1, node->children.end());

  RefreshInternalKeys(node);
  RefreshInternalKeys(right);
  return {true, promoted, right};
}

bool BPlusTree::DeleteFromNode(Node* node, int key, bool* deleted) {
  if (node->is_leaf) {
    // leaf에서 key를 찾아 실제로 제거한다.
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it == node->keys.end() || *it != key) {
      return false;
    }

    size_t pos = static_cast<size_t>(it - node->keys.begin());
    node->keys.erase(it);
    node->values.erase(node->values.begin() + static_cast<long long>(pos));
    *deleted = true;
    // leaf가 비었음을 부모에 알린다.
    return node->keys.empty();
  }

  // internal은 해당 자식으로 삭제를 위임한다.
  int idx = ChildIndex(node, key);
  bool child_empty = DeleteFromNode(node->children[idx], key, deleted);

  if (child_empty) {
    Node* child = node->children[idx];
    // leaf 제거 시 next 연결도 정리해야 RangeScan이 안전하다.
    if (child->is_leaf) {
      DetachLeafFromChain(child);
    }
    node->children.erase(node->children.begin() + idx);
    delete child;
  }

  if (node->children.empty()) {
    return true;
  }

  RefreshInternalKeys(node);
  return false;
}

void BPlusTree::RefreshInternalKeys(Node* node) {
  if (node == nullptr || node->is_leaf) {
    return;
  }

  // separator 규칙:
  // keys[i] = children[i+1] 서브트리의 최소 key
  node->keys.clear();
  for (size_t i = 1; i < node->children.size(); ++i) {
    // 해당 자식 서브트리의 최좌측 leaf를 찾아 최소 key를 가져온다.
    Node* cur = node->children[i];
    while (cur != nullptr && !cur->is_leaf && !cur->children.empty()) {
      cur = cur->children.front();
    }
    if (cur != nullptr && !cur->keys.empty()) {
      node->keys.push_back(cur->keys.front());
    }
  }
}

BPlusTree::Node* BPlusTree::FindLeaf(int key) const {
  Node* cur = root_;
  if (cur == nullptr) {
    return nullptr;
  }

  // internal을 따라 내려가 leaf에 도달한다.
  while (cur != nullptr && !cur->is_leaf) {
    int idx = ChildIndex(cur, key);
    if (idx < 0 || idx >= static_cast<int>(cur->children.size())) {
      return nullptr;
    }
    cur = cur->children[idx];
  }
  return cur;
}

BPlusTree::Node* BPlusTree::LeftmostLeaf() const {
  // 루트부터 항상 가장 왼쪽 자식으로 내려가 최좌측 leaf를 찾는다.
  Node* cur = root_;
  while (cur != nullptr && !cur->is_leaf && !cur->children.empty()) {
    cur = cur->children.front();
  }
  return cur;
}

void BPlusTree::DetachLeafFromChain(Node* target) {
  if (target == nullptr) {
    return;
  }

  // 단방향 leaf 체인에서 target의 이전 노드를 찾아 연결을 우회시킨다.
  Node* prev = nullptr;
  Node* cur = LeftmostLeaf();
  while (cur != nullptr && cur != target) {
    prev = cur;
    cur = cur->next;
  }

  if (cur == nullptr) {
    return;
  }

  if (prev != nullptr) {
    prev->next = cur->next;
  }
}

int BPlusTree::ChildIndex(const Node* node, int key) const {
  // upper_bound를 사용해 동일 key는 오른쪽 자식으로 내려가게 한다.
  // (keys[i]는 children[i+1]의 최소 key라는 separator 규칙과 대응)
  return static_cast<int>(
      std::upper_bound(node->keys.begin(), node->keys.end(), key) -
      node->keys.begin());
}
