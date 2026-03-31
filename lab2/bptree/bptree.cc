#include "bptree.h"

#include <algorithm>

// B+Tree Constructor. degree 설정 등 최초 설정 진행
BPlusTree::BPlusTree(int degree) : root_(nullptr), degree_(degree) {
  // code
}

// B+Tree Destructor. B+Tree에 존재하는 모든 Node delete 필요
BPlusTree::~BPlusTree() {
  // code
}

// B+Tree Put operation
void BPlusTree::Put(int key, const std::string& value) {
  // code
}

// B+Tree Get operation
bool BPlusTree::Get(int key, std::string* value) const {
  // code
  return false;
}

// B+Tree Range Scan operation
std::vector<std::pair<int, std::string>>
BPlusTree::RangeScan(int start_key, int end_key) const {
  std::vector<std::pair<int, std::string>> out;

  // code

  return out;
}

// B+Tree Delete operation. In-place update로 진행 됨으로, 실제 노드 삭제가
// 진행되야함
bool BPlusTree::Delete(int key) {

  // code

  return false;
}
