/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#include "sstable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct SSTableHeader {
  bool has_metadata = false;
  int smallest_key = 0;
  int largest_key = 0;
  int64_t oldest_seq = 0;
  int64_t newest_seq = 0;
  bool has_bloom_filter = false;
  BloomFilter bloom_filter;
};

// SSTable 헤더 메타데이터로부터 Bloom filter 생성
// 1. 엔트리 목록에서 키만 추출하여 Bloom filter 빌드
// 2. Bloom filter 메타데이터(비트 수, 해시 함수 개수) 포함
BloomFilter BuildBloomFilter(const std::vector<SSTableEntry>& entries,
                             size_t bits_per_key, size_t hash_count) {
  std::vector<int> keys;
  keys.reserve(entries.size());
  for (const auto& entry : entries) keys.push_back(entry.key);
  return BuildBloomFilterFromKeys(keys, bits_per_key, hash_count);
}

// SSTable 파일 읽기: 헤더 메타데이터 파싱
// 1. META 행 읽기: Bloom filter 활성화 여부, 키 범위, seq 범위 추출
// 2. BLOOM 행 읽기 (있으면): Bloom filter 비트 배열 디코딩
// 3. 데이터 행 이전까지 포지션 유지
bool ReadSSTableHeader(std::ifstream* in, SSTableHeader* header) {
  std::string line;
  if (!std::getline(*in, line)) return false;

  std::istringstream iss(line);
  std::string tag;
  int bloom_enabled = 0;
  if (!(iss >> tag >> bloom_enabled >> header->smallest_key >> header->largest_key >> header->oldest_seq >> header->newest_seq)) {
    return false;
  }
  if (tag != "META") return false;

  header->has_metadata = true;
  header->has_bloom_filter = (bloom_enabled != 0);

  std::streampos pos = in->tellg();
  if (std::getline(*in, line)) {
    if (line.rfind("BLOOM", 0) == 0) {
      std::istringstream biss(line);
      std::string bloom_tag;
      size_t bit_count = 0, hash_count = 0;
      std::string hex_bits;
      if (biss >> bloom_tag >> bit_count >> hash_count >> hex_bits) {
        header->bloom_filter.bit_count = bit_count;
        header->bloom_filter.hash_count = hash_count;
        if (!DecodeBloomFilterBits(hex_bits, &header->bloom_filter.bits)) {
          return false;
        }
        header->has_bloom_filter = true;
      }
    } else {
      in->clear();
      in->seekg(pos);
    }
  }
  return true;
}

// SSTable 데이터 행 파싱
// 형식: "P key seq value" (Put) 또는 "D key seq" (Delete/tombstone)
// 각 행에서 연산 타입, 키, seq, 값(있으면)을 추출
bool ParseSSTableDataLine(const std::string& line, SSTableEntry* entry) {
  std::istringstream iss(line);
  std::string op;
  if (!(iss >> op)) return false;

  if (op == "P") {
    if (!(iss >> entry->key >> entry->seq)) return false;
    if (!(iss >> entry->value)) entry->value = "";
    entry->tombstone = false;
    return true;
  }

  if (op == "D") {
    if (!(iss >> entry->key >> entry->seq)) return false;
    entry->value = "";
    entry->tombstone = true;
    return true;
  }

  return false;
}

// SSTable 파일명으로부터 파일 ID 추출
// 파일명 형식: "sst_<id>.txt" -> id 정수 파싱
std::optional<uint64_t> ParseSSTFileId(const std::string& name) {
  const std::string prefix = "sst_";
  const std::string suffix = ".txt";
  if (name.size() <= prefix.size() + suffix.size()) return std::nullopt;
  if (name.rfind(prefix, 0) != 0) return std::nullopt;
  if (name.substr(name.size() - suffix.size()) != suffix) return std::nullopt;
  const std::string number = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
  if (number.empty()) return std::nullopt;
  for (char c : number) if (c < '0' || c > '9') return std::nullopt;
  return static_cast<uint64_t>(std::stoull(number));
}

// 엔트리 목록으로부터 SSTable 헤더 메타데이터 생성
// 1. 모든 엔트리 순회하여 키 범위, seq 범위 계산
// 2. 최소/최대 키, 최소/최대 seq 값 추출
// 3. Bloom filter 메타데이터 포함
SSTableHeader BuildHeaderFromEntries(const std::vector<SSTableEntry>& entries,
                                     bool has_bloom_filter,
                                     BloomFilter bloom_filter) {
  SSTableHeader header;
  header.has_bloom_filter = has_bloom_filter;
  header.bloom_filter = std::move(bloom_filter);
  if (entries.empty()) throw std::invalid_argument("cannot write empty SSTable");

  header.has_metadata = true;
  header.smallest_key = entries.front().key;
  header.largest_key = entries.front().key;
  header.oldest_seq = entries.front().seq;
  header.newest_seq = entries.front().seq;
  for (const auto& e : entries) {
    header.smallest_key = std::min(header.smallest_key, e.key);
    header.largest_key = std::max(header.largest_key, e.key);
    header.oldest_seq = std::min(header.oldest_seq, e.seq);
    header.newest_seq = std::max(header.newest_seq, e.seq);
  }
  return header;
}

} // namespace


// SSTable 디렉토리 생성 함수
void EnsureSSTDir(const std::string& sst_dir) {
  std::filesystem::create_directories(sst_dir);
}

// SSTable 디렉토리 비어있는지 확인 함수
bool IsSSTDirEmpty(const std::string& sst_dir) {
  EnsureSSTDir(sst_dir);
  return std::filesystem::directory_iterator(sst_dir) == std::filesystem::directory_iterator();

}


// SSTable 파일 나열 및 로드 함수
// 주어진 디렉토리에서 모든 SSTable 파일을 찾아 로드
// 1. 디렉토리 순회하여 SSTable 파일명(sst_*.txt) 식별
// 2. 각 파일의 헤더 메타데이터 파싱
// 3. load_bloom_filter=true면 Bloom filter도 메모리에 로드
// 4. 파일 ID 순서로 정렬하여 반환
std::vector<SSTableFile> ListSSTables(const std::string& sst_dir,
                                     bool load_bloom_filter) {
  EnsureSSTDir(sst_dir);
  std::vector<SSTableFile> out;

  for (const auto& entry : std::filesystem::directory_iterator(sst_dir)) {
    if (!entry.is_regular_file()) continue;
    std::string filename = entry.path().filename().string();
    auto file_id = ParseSSTFileId(filename);
    if (!file_id.has_value()) continue;

    std::ifstream in(entry.path());
    if (!in) continue;

    SSTableHeader header;
    if (!ReadSSTableHeader(&in, &header)) {
      in.close();
      continue;
    }
    in.close();

    SSTableFile file;
    file.id = file_id.value();
    file.path = entry.path().string();
    file.smallest_key = header.smallest_key;
    file.largest_key = header.largest_key;
    file.oldest_seq = header.oldest_seq;
    file.newest_seq = header.newest_seq;
    file.has_bloom_filter = header.has_bloom_filter;
    if (load_bloom_filter && header.has_bloom_filter) {
      file.bloom_filter = header.bloom_filter;
    }

    out.push_back(file);
  }

  std::sort(out.begin(), out.end(), [](const SSTableFile& a, const SSTableFile& b) { return a.id < b.id; });
  return out;
}

// SSTable 파일 작성 함수
// 주어진 엔트리 목록으로 새로운 SSTable 파일 생성
// 1. 엔트리를 키 오름차순, seq 내림차순으로 정렬
// 2. 필요시 Bloom filter 생성
// 3. 헤더 메타데이터 구성
// 4. 파일 생성: META 행 -> BLOOM 행(선택) -> 데이터 행들
// 5. SSTableFile 메타데이터 반환
SSTableFile WriteSSTable(const std::string& sst_dir, uint64_t file_id,
                         const std::vector<SSTableEntry>& entries,
                         bool write_bloom_filter, size_t bloom_bits_per_key,
                         size_t bloom_hash_count) {
  if (entries.empty()) throw std::invalid_argument("cannot write empty SSTable");

  std::vector<SSTableEntry> sorted = entries;
  std::sort(sorted.begin(), sorted.end(), [](const SSTableEntry& a, const SSTableEntry& b){
    if (a.key != b.key) return a.key < b.key;
    return a.seq > b.seq;
  });

  BloomFilter bloom_filter;
  if (write_bloom_filter) bloom_filter = BuildBloomFilter(sorted, bloom_bits_per_key, bloom_hash_count);

  SSTableHeader header = BuildHeaderFromEntries(sorted, write_bloom_filter, bloom_filter);

  std::string filename = "sst_" + std::to_string(file_id) + ".txt";
  std::string filepath = std::filesystem::path(sst_dir) / filename;

  std::ofstream out(filepath);
  if (!out) throw std::runtime_error("failed to open SSTable file: " + filepath);

  out << "META " << (write_bloom_filter ? 1 : 0) << " " << header.smallest_key << " " << header.largest_key << " "
      << header.oldest_seq << " " << header.newest_seq << "\n";

  if (write_bloom_filter) {
    out << "BLOOM " << header.bloom_filter.bit_count << " "
        << header.bloom_filter.hash_count << " "
        << EncodeBloomFilterBits(header.bloom_filter.bits) << "\n";
  }

  for (const auto& e : sorted) {
    if (e.tombstone) {
      out << "D " << e.key << " " << e.seq << "\n";
    } else {
      out << "P " << e.key << " " << e.seq << " " << e.value << "\n";
    }
  }
  out.close();

  SSTableFile file;
  file.id = file_id;
  file.path = filepath;
  file.smallest_key = header.smallest_key;
  file.largest_key = header.largest_key;
  file.oldest_seq = header.oldest_seq;
  file.newest_seq = header.newest_seq;
  file.has_bloom_filter = write_bloom_filter;
  if (write_bloom_filter) {
    file.bloom_filter = header.bloom_filter;
  }
  return file;
}

// SSTable에서 키 조회 함수
// 주어진 키의 최신 버전을 찾아 값과 tombstone 상태 반환
// 1. 범위 검사: 키가 파일의 [smallest_key, largest_key] 범위 내인지 확인
// 2. Bloom filter 사전 검사: 있으면 MayContain으로 확인 (빠른 거절)
// 3. 파일 읽기: 모든 항목 순회하며 해당 키의 최대 seq 찾기
// 4. 최신 버전 반환 (여러 seq 버전이 있으면 최신만)
bool GetFromSSTable(const SSTableFile& file, int key, std::string* value,
                    bool* tombstone) {
  if (key < file.smallest_key || key > file.largest_key) return false;
  if (file.has_bloom_filter && !file.bloom_filter.Empty() &&
      !file.bloom_filter.MayContain(key)) {
    return false;
  }

  std::ifstream in(file.path);
  if (!in) return false;

  SSTableHeader header;
  if (!ReadSSTableHeader(&in, &header)) { in.close(); return false; }

  int64_t max_seq = -1;
  bool found = false;
  std::string line;
  while (std::getline(in, line)) {
    SSTableEntry e;
    if (!ParseSSTableDataLine(line, &e)) continue;
    if (e.key == key && e.seq > max_seq) {
      max_seq = e.seq;
      *value = e.value;
      *tombstone = e.tombstone;
      found = true;
    }
  }
  in.close();
  return found;
}

// SSTable에서 범위 스캔 함수
// 주어진 키 범위 [start_key, end_key]의 모든 엔트리 반환 (tombstone 포함)
// 1. 범위 검사: 범위가 파일 키 범위와 겹치는지 확인
// 2. 파일 읽기: 해당 범위의 모든 엔트리 수집
// 3. 키별 최신 버전 추출: 같은 키에 여러 seq 버전이 있으면 최신만
// 4. 결과 반환 (tombstone 포함, 상위 계층에서 필터링)
std::vector<SSTableEntry> RangeScanSSTable(const SSTableFile& file,
                                           int start_key, int end_key) {
  std::vector<SSTableEntry> out;
  std::ifstream in(file.path);
  if (!in) return out;

  SSTableHeader header;
  if (!ReadSSTableHeader(&in, &header)) { in.close(); return out; }

  if (end_key < header.smallest_key || start_key > header.largest_key) { in.close(); return out; }

  std::map<int, SSTableEntry> latest;
  std::string line;
  while (std::getline(in, line)) {
    SSTableEntry e;
    if (!ParseSSTableDataLine(line, &e)) continue;
    if (e.key < start_key || e.key > end_key) continue;
    auto it = latest.find(e.key);
    if (it == latest.end() || e.seq > it->second.seq) latest[e.key] = e;
  }
  in.close();

  for (const auto& p : latest) out.push_back(p.second);
  return out;
}
