#ifndef LAB2_BPTREE_H
#define LAB2_BPTREE_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// 필요시 내부 function, 변수 등 선언 가능

class BPlusTree {
public:
  explicit BPlusTree(int degree = 4);
  ~BPlusTree();

  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  void Put(int key, const std::string& value);
  bool Get(int key, std::string* value) const;
  bool Delete(int key);
  std::vector<std::pair<int, std::string>> RangeScan(int start_key,
                                                     int end_key) const;

private:
  struct Node;

  struct Node {
    explicit Node(bool leaf) : is_leaf(leaf), next(nullptr) {}

    bool is_leaf;
    std::vector<int> keys;
    std::vector<std::string> values;
    std::vector<Node*> children;
    Node* next;
  };

  struct SplitResult {
    bool split;
    int promoted_key;
    Node* right;
  };

  void Destroy(Node* node);
  SplitResult Insert(Node* node, int key, const std::string& value);
  bool DeleteFromNode(Node* node, int key, bool* deleted);
  void RefreshInternalKeys(Node* node);
  int ChildIndex(const Node* node, int key) const;
  Node* FindLeaf(int key) const;
  Node* LeftmostLeaf() const;
  void DetachLeafFromChain(Node* target);

  Node* root_;
  int degree_;
};

#endif // LAB2_BPTREE_H
