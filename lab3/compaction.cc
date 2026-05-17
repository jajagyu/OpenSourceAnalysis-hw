/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#include "compaction.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
// SSTable 파일 삭제 함수
// Compaction 완료 후 더 이상 필요 없는 기존 SSTable 파일 삭제
void RemoveSSTableFile(const SSTableFile& file) {
  std::filesystem::remove(file.path);
}
} // namespace

// SSTable Compaction 함수
// 1. 모든 파일 순회하여 모든 엔트리 수집: key -> (max_seq, entry) 맵 구성
// 2. 키별로 가장 최신 seq만 유지 (여러 버전이 있으면 최신만)
// 3. 최종 상태가 tombstone이 아닌 항목만 새 SSTable에 포함
// 4. 정렬된 상태로 새로운 SSTable 작성
// 5. 기존 파일 모두 삭제
std::optional<SSTableFile>
CompactAllSSTables(const std::string& sst_dir,
                   const std::vector<SSTableFile>& files,
                   uint64_t output_file_id, bool write_bloom_filter,
                   size_t bloom_bits_per_key, size_t bloom_hash_count) {

  if (files.empty()) {
    return std::nullopt;
  }
  std::map<int, std::pair<int64_t, SSTableEntry>> merged_entries;
  for (const auto& file : files) {
    std::ifstream in(file.path);
    if (!in) {
      continue;
    }
    std::string line;
    std::getline(in, line);  // META 라인

    if (std::getline(in, line)) {
      if (line.rfind("BLOOM", 0) == 0) {
      } else {
        {
          SSTableEntry entry;
          std::istringstream iss(line);
          std::string op;
          int key;
          int64_t seq;
          std::string value;
          if (iss >> op >> key >> seq) {
            entry.key = key;
            entry.seq = seq;
            entry.tombstone = (op == "D");
            if (!entry.tombstone && (iss >> value)) {
              entry.value = value;
            } else {
              entry.value = "";
            }
            auto it = merged_entries.find(key);
            if (it == merged_entries.end() || seq > it->second.first) {
              merged_entries[key] = {seq, entry};
            }
          }
        }
      }
    }

    while (std::getline(in, line)) {
      SSTableEntry entry;
      std::istringstream iss(line);
      std::string op;
      int key;
      int64_t seq;
      std::string value;

      if (!(iss >> op >> key >> seq)) {
        continue;
      }

      entry.key = key;
      entry.seq = seq;
      entry.tombstone = (op == "D");

      if (!entry.tombstone && (iss >> value)) {
        entry.value = value;
      } else {
        entry.value = "";
      }

      auto it = merged_entries.find(key);
      if (it == merged_entries.end() || seq > it->second.first) {
        merged_entries[key] = {seq, entry};
      }
    }
    in.close();
  }
  std::vector<SSTableEntry> compacted_entries;
  for (const auto& pair : merged_entries) {
    if (!pair.second.second.tombstone) {
      compacted_entries.push_back(pair.second.second);
    }
  }
  if (compacted_entries.empty()) {
    for (const auto& file : files) {
      RemoveSSTableFile(file);
    }
    return std::nullopt;
  }
  SSTableFile new_file = WriteSSTable(sst_dir, output_file_id, compacted_entries,
                                      write_bloom_filter, bloom_bits_per_key,
                                      bloom_hash_count);
  for (const auto& file : files) {
    RemoveSSTableFile(file);
  }
  std::optional<SSTableFile> compacted_file;
  compacted_file = new_file;
  return compacted_file;
}
