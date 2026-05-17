/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#ifndef LAB3_COMPACTION_H
#define LAB3_COMPACTION_H

#include "sstable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// 모든 SSTable 파일 병합 및 Compaction 함수
// 1. 모든 파일에서 모든 엔트리 읽기
// 2. 키별로 최신 seq 버전만 유지 (구 버전과 중복 제거)
// 3. Tombstone으로 표시된 삭제된 항목 제거
// 4. 정렬된 상태로 새로운 SSTable에 작성
// 5. 기존 파일 삭제
// 반환: 병합된 새 파일 (모든 항목이 삭제되면 std::nullopt)
std::optional<SSTableFile> CompactAllSSTables(
    const std::string& sst_dir, const std::vector<SSTableFile>& files,
    uint64_t output_file_id, bool write_bloom_filter,
    size_t bloom_bits_per_key, size_t bloom_hash_count);

#endif  // LAB3_COMPACTION_H
