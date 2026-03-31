#include "bptree.h"

#include <algorithm>

// B+Tree Constructor. degree 설정 등 최초 설정 진행
BPlusTree::BPlusTree(int degree) : root_(nullptr), degree_(degree) {
  if (degree_ < 3) {
    degree_ = 3;
  }
  root_ = new Node(true);
}

// B+Tree Destructor. B+Tree에 존재하는 모든 Node delete 필요
BPlusTree::~BPlusTree() {
  Destroy(root_);
  root_ = nullptr;
}

// B+Tree Put operation
void BPlusTree::Put(int key, const std::string& value) {
  SplitResult res = Insert(root_, key, value);
  if (!res.split) {
    return;
  }

  Node* new_root = new Node(false);
  new_root->children.push_back(root_);
  new_root->children.push_back(res.right);
  new_root->keys.push_back(res.promoted_key);
  root_ = new_root;
}

// B+Tree Get operation
bool BPlusTree::Get(int key, std::string* value) const {
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

  if (start_key > end_key) {
    return out;
  }

  Node* leaf = FindLeaf(start_key);
  while (leaf != nullptr) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      int key = leaf->keys[i];
      if (key < start_key) {
        continue;
      }
      if (key > end_key) {
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

  if (!root_->is_leaf && root_->children.empty()) {
    delete root_;
    root_ = new Node(true);
    return deleted;
  }

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

  if (!node->is_leaf) {
    for (Node* child : node->children) {
      Destroy(child);
    }
  }
  delete node;
}

BPlusTree::SplitResult BPlusTree::Insert(Node* node, int key,
                                         const std::string& value) {
  const int max_keys = degree_ - 1;

  if (node->is_leaf) {
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    size_t pos = static_cast<size_t>(it - node->keys.begin());

    if (it != node->keys.end() && *it == key) {
      node->values[pos] = value;
      return {false, 0, nullptr};
    }

    node->keys.insert(it, key);
    node->values.insert(node->values.begin() + static_cast<long long>(pos),
                        value);

    if (static_cast<int>(node->keys.size()) <= max_keys) {
      return {false, 0, nullptr};
    }

    int mid = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->values.assign(node->values.begin() + mid, node->values.end());

    node->keys.erase(node->keys.begin() + mid, node->keys.end());
    node->values.erase(node->values.begin() + mid, node->values.end());

    right->next = node->next;
    node->next = right;

    return {true, right->keys.front(), right};
  }

  int idx = ChildIndex(node, key);
  SplitResult child_res = Insert(node->children[idx], key, value);
  if (!child_res.split) {
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

  node->keys.insert(node->keys.begin() + idx, child_res.promoted_key);
  node->children.insert(node->children.begin() + idx + 1, child_res.right);

  if (static_cast<int>(node->keys.size()) <= max_keys) {
    RefreshInternalKeys(node);
    return {false, 0, nullptr};
  }

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
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it == node->keys.end() || *it != key) {
      return false;
    }

    size_t pos = static_cast<size_t>(it - node->keys.begin());
    node->keys.erase(it);
    node->values.erase(node->values.begin() + static_cast<long long>(pos));
    *deleted = true;
    return node->keys.empty();
  }

  int idx = ChildIndex(node, key);
  bool child_empty = DeleteFromNode(node->children[idx], key, deleted);

  if (child_empty) {
    Node* child = node->children[idx];
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

  node->keys.clear();
  for (size_t i = 1; i < node->children.size(); ++i) {
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
  return static_cast<int>(
      std::upper_bound(node->keys.begin(), node->keys.end(), key) -
      node->keys.begin());
}
