/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#ifndef LAB3_SSTABLE_H
#define LAB3_SSTABLE_H

#include "bloom_filter.h"

#include <cstdint>
#include <string>
#include <vector>

struct SSTableFile {
  uint64_t id;
  std::string path;
  int smallest_key = 0;          // 파일 내 최소 키
  int largest_key = 0;           // 파일 내 최대 키 (범위 검색 최적화용)
  int64_t oldest_seq = 0;        // 파일 내 가장 오래된 sequence number
  int64_t newest_seq = 0;        // 파일 내 가장 최신 sequence number
  bool has_bloom_filter = false; // Bloom filter 포함 여부
  BloomFilter bloom_filter;
};

struct SSTableEntry {
  int key;              // 엔트리 키
  int64_t seq;          // 엔트리 sequence number (클수록 최신)
  std::string value;    // 엔트리 값 (tombstone인 경우 비어있음)
  bool tombstone;       // 삭제 마커 (true면 이 키가 삭제된 상태)
};

// SSTable 디렉토리 생성 및 관리
// 주어진 경로에 SSTable 저장 디렉토리 생성
void EnsureSSTDir(const std::string& sst_dir);

// SSTable 디렉토리 비어있는지 확인
// 기존 DB를 재사용할 것인지 새로 생성할 것인지 판단하는 데 사용
bool IsSSTDirEmpty(const std::string& sst_dir);

// 주어진 디렉토리의 모든 SSTable 파일 나열 및 로드
// load_bloom_filter: true면 각 파일의 Bloom filter까지 메모리에 로드
// 파일은 ID 순서대로 정렬되어 반환됨
std::vector<SSTableFile> ListSSTables(const std::string& sst_dir,
                                     bool load_bloom_filter = false);

// 주어진 엔트리 목록으로 새로운 SSTable 파일 생성 및 작성
// 1. 엔트리를 키 순서대로 정렬 (같은 키는 seq 내림차순)
// 2. 필요시 Bloom filter 생성
// 3. 메타데이터와 데이터 행을 파일에 작성
// 반환: 생성된 SSTable 파일 정보
SSTableFile WriteSSTable(const std::string& sst_dir, uint64_t file_id,
                         const std::vector<SSTableEntry>& entries,
                         bool write_bloom_filter = false,
                         size_t bloom_bits_per_key = 10,
                         size_t bloom_hash_count = 6);

// 주어진 SSTable에서 특정 키 조회
// 범위 검사 및 Bloom filter로 먼저 검사 후 파일 읽기 수행
// 여러 seq 버전이 있으면 최신 버전(최대 seq)을 반환
bool GetFromSSTable(const SSTableFile& file, int key, std::string* value,
                    bool* tombstone);

// 주어진 SSTable에서 키 범위 스캔
// [start_key, end_key] 범위의 최신 엔트리를 반환 (tombstone 포함)
// 여러 seq 버전이 있으면 각 키당 최신 버전만 반환
std::vector<SSTableEntry> RangeScanSSTable(const SSTableFile& file,
                                           int start_key, int end_key);

#endif  // LAB3_SSTABLE_H
