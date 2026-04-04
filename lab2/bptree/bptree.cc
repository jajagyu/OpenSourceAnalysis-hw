#include "bptree.h"

#include <algorithm>

// B+Tree Constructor, degree 설정 등 최초 설정 진행
BPlusTree::BPlusTree(int degree) : root_(nullptr), degree_(degree) {
  // 너무 작은 degree는 분기 의미가 약해지므로 최소 3으로 보정
  if (degree_ < 3) {
    degree_ = 3;
  }
  // 초기에는 비어 있는 단일 leaf 루트로 시작
  root_ = new Node(true);
}

// B+Tree Destructor, B+Tree에 존재하는 모든 Node delete 필요
BPlusTree::~BPlusTree() {
  // 루트부터 재귀 해제
  Destroy(root_);
  root_ = nullptr;
}

// B+Tree Put operation
void BPlusTree::Put(int key, const std::string& value) {
  // 하향 삽입 결과를 받고, 루트가 split 할 필요가 있으면 새 루트를 만듦
  SplitResult res = Insert(root_, key, value);
  if (!res.split) {
    return;
  }

  // old root, new right를 자식으로 갖는 internal root 생성
  Node* new_root = new Node(false);
  new_root->children.push_back(root_);
  new_root->children.push_back(res.right);
  new_root->keys.push_back(res.promoted_key);
  
  // parent 포인터 설정
  root_->parent = new_root;
  res.right->parent = new_root;
  
  root_ = new_root;
}

// B+Tree Get operation
bool BPlusTree::Get(int key, std::string* value) const {
  // key가 속할 leaf를 찾고, leaf 내부에서 이진 탐색
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

  // 역방향/빈 범위 방어
  if (start_key > end_key) {
    return out;
  }

  // 시작 key가 속한 leaf부터 오른쪽 leaf 체인을 순회
  Node* leaf = FindLeaf(start_key);
  while (leaf != nullptr) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      int key = leaf->keys[i];
      if (key < start_key) {
        continue;
      }
      if (key > end_key) {
        // leaf가 정렬되어 있으므로 end_key 초과 시 즉시 종료 가능
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
  bool deleted = false;
  DeleteFromNode(root_, key, &deleted);

  // 루트가 internal인데 자식이 비면 새 빈 leaf 루트로 초기화
  if (!root_->is_leaf && root_->children.empty()) {
    delete root_;
    root_ = new Node(true);
    return deleted;
  }

  // 루트 축소: internal root의 자식이 하나만 남으면 높이를 줄임
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

  // internal 노드는 자식을 먼저 재귀적으로 해제
  if (!node->is_leaf) {
    for (Node* child : node->children) {
      Destroy(child);
    }
  }
  delete node;
}

BPlusTree::SplitResult BPlusTree::Insert(Node* node, int key,
                                         const std::string& value) {
  // 한 노드가 가질 수 있는 최대 key 수
  const int max_keys = degree_ - 1;

  if (node->is_leaf) {
    // leaf에서 key 위치를 이진 탐색
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    size_t pos = static_cast<size_t>(it - node->keys.begin());

    // in-place update: 기존 key면 값만 갱신
    if (it != node->keys.end() && *it == key) {
      node->values[pos] = value;
      return {false, 0, nullptr};
    }

    // 새 key/value 삽입
    node->keys.insert(it, key);
    node->values.insert(node->values.begin() + static_cast<long long>(pos),
                        value);

    // overflow가 아니면 종료
    if (static_cast<int>(node->keys.size()) <= max_keys) {
      return {false, 0, nullptr};
    }

    // leaf split: 중간 기준으로 오른쪽 leaf를 분리
    int mid = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->values.assign(node->values.begin() + mid, node->values.end());

    node->keys.erase(node->keys.begin() + mid, node->keys.end());
    node->values.erase(node->values.begin() + mid, node->values.end());

    // leaf 연결 리스트 유지
    right->next = node->next;
    node->next = right;

    // B+Tree leaf split에서 부모로 올리는 키는 right의 최소 key
    return {true, right->keys.front(), right};
  }

  // internal: key가 들어갈 자식으로 재귀 삽입
  int idx = ChildIndex(node, key);
  SplitResult child_res = Insert(node->children[idx], key, value);
  if (!child_res.split) {
    // 자식 변경으로 separator가 달라질 수 있으므로 재계산
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

  // 자식 split 반영: 승격 키와 오른쪽 자식을 삽입
  node->keys.insert(node->keys.begin() + idx, child_res.promoted_key);
  node->children.insert(node->children.begin() + idx + 1, child_res.right);
  
  // split된 자식의 parent 포인터 설정
  child_res.right->parent = node;

  if (static_cast<int>(node->keys.size()) <= max_keys) {
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

  // internal split: mid key는 부모로 승격되고, 양쪽 노드에서 제거
  int mid = static_cast<int>(node->keys.size()) / 2;
  int promoted = node->keys[mid];
  Node* right = new Node(false);

  right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
  right->children.assign(node->children.begin() + mid + 1, node->children.end());

  node->keys.erase(node->keys.begin() + mid, node->keys.end());
  node->children.erase(node->children.begin() + mid + 1, node->children.end());

  // right의 모든 자식의 parent 포인터를 right로 설정
  for (Node* child : right->children) {
    child->parent = right;
  }

  RefreshInternalKeys(node);
  RefreshInternalKeys(right);
  return {true, promoted, right};
}

bool BPlusTree::DeleteFromNode(Node* node, int key, bool* deleted) {
  if (node->is_leaf) {
    // leaf에서 key를 찾아 실제로 제거
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it == node->keys.end() || *it != key) {
      return false;
    }

    size_t pos = static_cast<size_t>(it - node->keys.begin());
    node->keys.erase(it);
    node->values.erase(node->values.begin() + static_cast<long long>(pos));
    *deleted = true;
    // leaf가 비었다면 true반환 (부모가 자식 제거 여부 판단용)
    return node->keys.empty();
  }

  // internal은 해당 자식으로 삭제를 위임
  int idx = ChildIndex(node, key);
  bool child_empty = DeleteFromNode(node->children[idx], key, deleted);

  if (child_empty) {
    Node* child = node->children[idx];
    // leaf 제거 시 next 연결도 정리해야 RangeScan이 안전함
    if (child->is_leaf) {
      DetachLeafFromChain(child);
    }
    node->children.erase(node->children.begin() + idx);
    delete child;
  } else if (*deleted) {
    // 자식에서 삭제가 발생했으면 언더플로우 체크
    int min_keys = MinKeys(node->children[idx]->is_leaf);
    if (static_cast<int>(node->children[idx]->keys.size()) < min_keys) {
      FixUnderflow(node->children[idx], idx);
    }
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
    // 해당 자식 서브트리의 가장 왼쪽 leaf를 찾아 최소 key를 가져옴
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

  // internal을 따라 내려가 leaf에 도달
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
  // 루트부터 항상 가장 왼쪽 자식으로 내려가 가장 왼쪽 leaf를 찾음
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

  // 단방향 leaf 체인에서 target의 이전 노드를 찾아 연결을 우회시킴
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
  // upper_bound를 사용해 동일 key는 오른쪽 자식으로 내려가게 함
  // (keys[i]는 children[i+1]의 최소 key라는 separator 규칙과 대응)
  return static_cast<int>(
      std::upper_bound(node->keys.begin(), node->keys.end(), key) -
      node->keys.begin());
}

int BPlusTree::MinKeys(bool is_leaf) const {
  // 리프 노드: 최소 ⌊degree / 2⌋개의 키
  // 내부 노드: 최소 ⌈degree / 2⌉ - 1개의 키 (최소 ⌈degree / 2⌉개의 자식)
  if (is_leaf) {
    return degree_ / 2;
  } else {
    return (degree_ + 1) / 2 - 1;
  }
}

void BPlusTree::FixUnderflow(Node* child, int child_index) {
  if (child->parent == nullptr) {
    return; // 루트는 언더플로우 처리 불필요
  }

  Node* parent = child->parent;
  int min_keys = MinKeys(child->is_leaf);

  // 왼쪽 형제에서 빌려오기 시도
  if (child_index > 0) {
    Node* left_sibling = parent->children[child_index - 1];
    if (static_cast<int>(left_sibling->keys.size()) > min_keys) {
      BorrowFromSibling(child, child_index);
      return;
    }
  }

  // 오른쪽 형제에서 빌려오기 시도
  if (child_index < static_cast<int>(parent->children.size()) - 1) {
    Node* right_sibling = parent->children[child_index + 1];
    if (static_cast<int>(right_sibling->keys.size()) > min_keys) {
      BorrowFromSibling(child, child_index);
      return;
    }
  }

  // 형제가 최소 키만 가지고 있아서 빌려올 수 없으면 합병
  MergeWithSibling(child, child_index);
}

bool BPlusTree::BorrowFromSibling(Node* child, int child_index) {
  Node* parent = child->parent;

  if (child->is_leaf) {
    // 왼쪽 형제에서 빌려오기
    if (child_index > 0) {
      Node* left_sibling = parent->children[child_index - 1];
      if (static_cast<int>(left_sibling->keys.size()) > MinKeys(true)) {
        // 왼쪽 형제의 마지막 키를 자식의 맨 앞으로 이동
        child->keys.insert(child->keys.begin(), left_sibling->keys.back());
        child->values.insert(child->values.begin(), left_sibling->values.back());
        left_sibling->keys.pop_back();
        left_sibling->values.pop_back();
        
        // parent의 separator key 업데이트
        parent->keys[child_index - 1] = child->keys.front();
        return true;
      }
    }

    // 오른쪽 형제에서 빌려오기
    if (child_index < static_cast<int>(parent->children.size()) - 1) {
      Node* right_sibling = parent->children[child_index + 1];
      if (static_cast<int>(right_sibling->keys.size()) > MinKeys(true)) {
        // 오른쪽 형제의 첫 번째 키를 자식의 맨 뒤로 이동
        child->keys.push_back(right_sibling->keys.front());
        child->values.push_back(right_sibling->values.front());
        right_sibling->keys.erase(right_sibling->keys.begin());
        right_sibling->values.erase(right_sibling->values.begin());
        
        // parent의 separator key 업데이트
        parent->keys[child_index] = right_sibling->keys.front();
        return true;
      }
    }
  } else {
    // 내부 노드의 경우도 유사하게 처리
    // 왼쪽 형제에서 빌려오기
    if (child_index > 0) {
      Node* left_sibling = parent->children[child_index - 1];
      if (static_cast<int>(left_sibling->keys.size()) > MinKeys(false)) {
        // 왼쪽 형제의 마지막 키와 자식을 자식으로 이동
        int moved_key = left_sibling->keys.back();
        Node* moved_child = left_sibling->children.back();
        
        child->keys.insert(child->keys.begin(), parent->keys[child_index - 1]);
        child->children.insert(child->children.begin(), moved_child);
        moved_child->parent = child;
        
        parent->keys[child_index - 1] = moved_key;
        left_sibling->keys.pop_back();
        left_sibling->children.pop_back();
        return true;
      }
    }

    // 오른쪽 형제에서 빌려오기
    if (child_index < static_cast<int>(parent->children.size()) - 1) {
      Node* right_sibling = parent->children[child_index + 1];
      if (static_cast<int>(right_sibling->keys.size()) > MinKeys(false)) {
        // 오른쪽 형제의 첫 번째 키와 자식을 자식으로 이동
        int moved_key = right_sibling->keys.front();
        Node* moved_child = right_sibling->children.front();
        
        child->keys.push_back(parent->keys[child_index]);
        child->children.push_back(moved_child);
        moved_child->parent = child;
        
        parent->keys[child_index] = moved_key;
        right_sibling->keys.erase(right_sibling->keys.begin());
        right_sibling->children.erase(right_sibling->children.begin());
        return true;
      }
    }
  }

  return false;
}

void BPlusTree::MergeWithSibling(Node* child, int child_index) {
  Node* parent = child->parent;

  if (child->is_leaf) {
    // 왼쪽 형제와 합병 시도
    if (child_index > 0) {
      Node* left_sibling = parent->children[child_index - 1];
      // 왼쪽 형제에 자식의 모든 키/값을 합병
      left_sibling->keys.insert(left_sibling->keys.end(), child->keys.begin(),
                                child->keys.end());
      left_sibling->values.insert(left_sibling->values.end(), child->values.begin(),
                                  child->values.end());
      
      // leaf 연결 리스트 유지
      left_sibling->next = child->next;
      
      // parent에서 separator key 제거
      parent->keys.erase(parent->keys.begin() + child_index - 1);
      parent->children.erase(parent->children.begin() + child_index);
      delete child;
      return;
    }

    // 오른쪽 형제와 합병 (자식이 왼쪽이 됨)
    if (child_index < static_cast<int>(parent->children.size()) - 1) {
      Node* right_sibling = parent->children[child_index + 1];
      // 자식에 오른쪽 형제의 모든 키/값을 합병
      child->keys.insert(child->keys.end(), right_sibling->keys.begin(),
                         right_sibling->keys.end());
      child->values.insert(child->values.end(), right_sibling->values.begin(),
                           right_sibling->values.end());
      
      // leaf 연결 리스트 유지
      child->next = right_sibling->next;
      
      // parent에서 separator key 제거
      parent->keys.erase(parent->keys.begin() + child_index);
      parent->children.erase(parent->children.begin() + child_index + 1);
      delete right_sibling;
      return;
    }
  } else {
    // 왼쪽 형제와 합병 시도
    if (child_index > 0) {
      Node* left_sibling = parent->children[child_index - 1];
      // separator key를 왼쪽 형제에 추가
      left_sibling->keys.push_back(parent->keys[child_index - 1]);
      // 자식의 모든 키와 자식을 왼쪽 형제에 합병
      left_sibling->keys.insert(left_sibling->keys.end(), child->keys.begin(),
                                child->keys.end());
      left_sibling->children.insert(left_sibling->children.end(),
                                    child->children.begin(),
                                    child->children.end());
      
      // 병합된 자식들의 parent 포인터 업데이트
      for (Node* grandchild : child->children) {
        grandchild->parent = left_sibling;
      }
      
      // parent에서 separator key 제거
      parent->keys.erase(parent->keys.begin() + child_index - 1);
      parent->children.erase(parent->children.begin() + child_index);
      delete child;
      return;
    }

    // 오른쪽 형제와 합병
    if (child_index < static_cast<int>(parent->children.size()) - 1) {
      Node* right_sibling = parent->children[child_index + 1];
      // separator key를 자식에 추가
      child->keys.push_back(parent->keys[child_index]);
      // 오른쪽 형제의 모든 키와 자식을 자식에 합병
      child->keys.insert(child->keys.end(), right_sibling->keys.begin(),
                         right_sibling->keys.end());
      child->children.insert(child->children.end(), right_sibling->children.begin(),
                             right_sibling->children.end());
      
      // 병합된 자식들의 parent 포인터 업데이트
      for (Node* grandchild : right_sibling->children) {
        grandchild->parent = child;
      }
      
      // parent에서 separator key 제거
      parent->keys.erase(parent->keys.begin() + child_index);
      parent->children.erase(parent->children.begin() + child_index + 1);
      delete right_sibling;
      return;
    }
  }
}
