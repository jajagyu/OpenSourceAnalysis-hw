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
void RemoveSSTableFile(const SSTableFile& file) {

  // Compaction 완료 후 SSTable 삭제
  std::filesystem::remove(file.path);
}
} // namespace

std::optional<SSTableFile>
CompactAllSSTables(const std::string& sst_dir,
                   const std::vector<SSTableFile>& files,
                   uint64_t output_file_id, bool write_bloom_filter,
                   size_t bloom_bits_per_key, size_t bloom_hash_count) {

  // 모든 SSTable Compaction 진행
  // 1. 모든 파일에서 모든 항목 수집
  // 2. 키별로 최신 seq만 유지
  // 3. 키 순서대로 정렬
  // 4. 새로운 SSTable 작성
  // 5. 기존 파일 삭제
  
  if (files.empty()) {
    return std::nullopt;
  }
  
  // 모든 항목을 수집: key -> (seq, entry)
  std::map<int, std::pair<int64_t, SSTableEntry>> merged_entries;
  
  // 각 SSTable에서 모든 항목 읽기
  for (const auto& file : files) {
    std::ifstream in(file.path);
    if (!in) {
      continue;
    }
    
    // 헤더 읽기
    std::string line;
    std::getline(in, line);  // META 라인

    // 다음 라인 읽기: BLOOM 라인이면 건너뛰고, 아니면 그 라인을 먼저 처리
    if (std::getline(in, line)) {
      if (line.rfind("BLOOM", 0) == 0) {
        // BLOOM 라인이므로 무시하고 다음 라인부터 데이터 처리
      } else {
        // 이 라인은 데이터 라인임: 먼저 처리
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

    // 모든 데이터 라인 읽기 (이미 첫 데이터 라인을 처리했을 수 있음)
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

      // 해당 key의 최신 seq 항목만 유지
      auto it = merged_entries.find(key);
      if (it == merged_entries.end() || seq > it->second.first) {
        merged_entries[key] = {seq, entry};
      }
    }
    
    in.close();
  }
  
  // merged_entries를 SSTableEntry 벡터로 변환 (이미 key 순서대로 정렬됨)
  // 단, tombstone이 아닌 항목만 포함 (최신 상태가 삭제된 경우는 스킵)
  std::vector<SSTableEntry> compacted_entries;
  for (const auto& pair : merged_entries) {
    // 최종 상태가 tombstone이 아닌 경우만 포함
    if (!pair.second.second.tombstone) {
      compacted_entries.push_back(pair.second.second);
    }
  }
  
  // 새로운 SSTable 작성
  if (compacted_entries.empty()) {
    // 모든 항목이 삭제된 경우: 파일만 제거하고 빈 SSTable 반환
    for (const auto& file : files) {
      RemoveSSTableFile(file);
    }
    return std::nullopt;
  }
  
  SSTableFile new_file = WriteSSTable(sst_dir, output_file_id, compacted_entries,
                                      write_bloom_filter, bloom_bits_per_key,
                                      bloom_hash_count);
  
  // 기존 파일 삭제
  for (const auto& file : files) {
    RemoveSSTableFile(file);
  }
  
  std::optional<SSTableFile> compacted_file;
  compacted_file = new_file;
  return compacted_file;
}
