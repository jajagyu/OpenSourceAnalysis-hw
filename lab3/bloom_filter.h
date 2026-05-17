/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#ifndef LAB3_BLOOM_FILTER_H
#define LAB3_BLOOM_FILTER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct BloomFilter {
  size_t bit_count = 0;
  size_t hash_count = 0;
  std::vector<uint8_t> bits;

  void Add(int key);
  bool MayContain(int key) const;
  bool Empty() const;
};

// Bloom filter 비트 수를 바이트 수로 변환하는 함수
size_t BloomByteSize(size_t bit_count);

// Bloom filter 바이트 배열을 16진수 문자열로 인코딩하는 함수
// SSTable에 저장할 때 텍스트 형식으로 변환 필요
std::string EncodeBloomFilterBits(const std::vector<uint8_t>& bytes);

// 16진수 문자열로 인코딩된 Bloom filter를 바이트 배열로 디코딩하는 함수
// SSTable에서 저장된 텍스트를 읽어 복원할 때 사용
bool DecodeBloomFilterBits(const std::string& encoded,
                           std::vector<uint8_t>* out);

// 주어진 키 목록으로부터 Bloom filter를 생성하는 함수
// bits_per_key: 키당 할당할 비트 수 (필터 정확도에 영향)
// hash_count: 사용할 해시 함수의 개수
BloomFilter BuildBloomFilterFromKeys(const std::vector<int>& keys,
                                     size_t bits_per_key,
                                     size_t hash_count);

#endif  // LAB3_BLOOM_FILTER_H
