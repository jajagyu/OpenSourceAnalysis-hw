#ifndef LAB2_BPTREE_H
#define LAB2_BPTREE_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// 필요시 내부 function, 변수 등 선언 가능

class BPlusTree {
public:
  // degree는 한 노드가 가질 수 있는 최대 자식 수를 의미한다.
  // 최대 key 수는 degree - 1로 사용한다.
  explicit BPlusTree(int degree = 4);
  ~BPlusTree();

  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  // 같은 key가 이미 존재하면 값을 덮어쓰는 in-place update.
  // 존재하지 않으면 삽입하며, overflow 시 split을 전파한다.
  void Put(int key, const std::string& value);

  // key 단건 조회.
  bool Get(int key, std::string* value) const;

  // key를 실제 leaf 엔트리에서 제거한다 (tombstone 미사용).
  bool Delete(int key);

  // [start_key, end_key] 범위를 leaf 연결(next) 기반으로 순차 스캔한다.
  std::vector<std::pair<int, std::string>> RangeScan(int start_key,
                                                     int end_key) const;

private:
  struct Node;

  // B+Tree 공통 노드.
  // is_leaf=true: keys/values 사용, children 비사용
  // is_leaf=false: keys/children 사용, values 비사용
  struct Node {
    explicit Node(bool leaf) : is_leaf(leaf), next(nullptr) {}

    // leaf/internal 구분.
    bool is_leaf;

    // 정렬된 분리 키들.
    std::vector<int> keys;

    // leaf에서 keys와 1:1 대응하는 값들.
    std::vector<std::string> values;

    // internal에서 자식 포인터들. 일반적으로 children.size() = keys.size() + 1.
    std::vector<Node*> children;

    // leaf 노드의 우측 형제 포인터. RangeScan 최적화를 위해 사용.
    Node* next;
  };

  // 하위 삽입 결과를 부모에 전달하기 위한 구조체.
  // split=true면 오른쪽 새 노드와 승격 키를 부모가 반영해야 한다.
  struct SplitResult {
    bool split;
    int promoted_key;
    Node* right;
  };

  // 서브트리 전체 해제.
  void Destroy(Node* node);

  // 재귀 삽입. split 발생 시 승격 정보를 반환.
  SplitResult Insert(Node* node, int key, const std::string& value);

  // 재귀 삭제. true 반환은 "해당 자식 노드가 비어서 부모에서 제거 필요" 의미.
  bool DeleteFromNode(Node* node, int key, bool* deleted);

  // internal 노드의 separator key를 자식 상태 기준으로 다시 계산.
  void RefreshInternalKeys(Node* node);

  // key가 들어갈 자식 인덱스 계산 (upper_bound 기반).
  int ChildIndex(const Node* node, int key) const;

  // key가 있어야 할 leaf까지 탐색.
  Node* FindLeaf(int key) const;

  // 전체 트리의 가장 왼쪽 leaf.
  Node* LeftmostLeaf() const;

  // leaf 삭제 시 연결 리스트(next)에서 해당 leaf를 분리.
  void DetachLeafFromChain(Node* target);

  // 현재 루트 노드.
  Node* root_;
  // 노드 fanout 설정값.
  int degree_;
};

#endif // LAB2_BPTREE_H
