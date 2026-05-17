/*
 * ※ 참고 사항
 * 본 과제에서 기본으로 제공된 스켈레톤 코드의 경우,
 * 전체적인 시스템 흐름과 내부 동작을 완벽히 이해하기 위해 주석을 추가해 두었습니다.
 */
#include "bloom_filter.h"

#include <algorithm>
#include <cstdint>

namespace { // Bloom filter 제작에 필요한 함수들
size_t PositiveModulo(uint64_t value, size_t mod) { 
  return static_cast<size_t>(value % static_cast<uint64_t>(mod));
}

uint64_t MixKey(int key, uint64_t seed) {
  uint64_t x = static_cast<uint32_t>(key);
  x ^= seed + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

uint64_t SimpleHash(int key, size_t hash_function_index) {
  const uint64_t seed =
      0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(hash_function_index + 1);
  return MixKey(key, seed);

}

void SetBit(std::vector<uint8_t>* bits, size_t index) {
  (*bits)[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
}

bool GetBit(const std::vector<uint8_t>& bits, size_t index) {
  return (bits[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
}

char NibbleToHex(uint8_t value) {
  return static_cast<char>(value < 10 ? ('0' + value) : ('a' + value - 10));
}

bool HexToNibble(char c, uint8_t* value) {
  if (c >= '0' && c <= '9') {
    *value = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *value = static_cast<uint8_t>(10 + c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *value = static_cast<uint8_t>(10 + c - 'A');
    return true;
  }
  return false;
}
}  // namespace

size_t BloomByteSize(size_t bit_count) { return (bit_count + 7) / 8; }


// Bloom filter Encoding
// Bloom filter 바이트 배열을 텍스트 형식으로 변환
// 각 바이트를 2자리 16진수 문자열로 변환하여 저장 가능한 형식으로 변환
std::string EncodeBloomFilterBits(const std::vector<uint8_t>& bytes) {
  std::string out;
  for (uint8_t byte : bytes) {
    out += NibbleToHex(byte >> 4);    // 상위 4비트
    out += NibbleToHex(byte & 0x0f);  // 하위 4비트
  }

  return out;
}

// Bloom filter Decoding
// 텍스트 형식의 16진수 문자열을 바이트 배열로 변환
// SSTable에 저장된 Bloom filter를 메모리에 복원할 때 사용
bool DecodeBloomFilterBits(const std::string& encoded,
                           std::vector<uint8_t>* out) {
  if (encoded.size() % 2 != 0) {
    return false;
  }
  out->clear();
  for (size_t i = 0; i < encoded.size(); i += 2) {
    uint8_t high, low;
    if (!HexToNibble(encoded[i], &high) || !HexToNibble(encoded[i + 1], &low)) {
      return false;
    }
    out->push_back((high << 4) | low);
  }

  return true;
}

// Bloom filter 구성 함수
// 주어진 키 목록으로부터 Bloom filter 구조를 생성
// 1. 필요한 전체 비트 수 계산 (키 개수 × bits_per_key)
// 2. 비트 배열 초기화
// 3. 각 키를 필터에 추가
BloomFilter BuildBloomFilterFromKeys(const std::vector<int>& keys,
                                     size_t bits_per_key,
                                     size_t hash_count) {
  BloomFilter filter;
  if (keys.empty() || bits_per_key == 0 || hash_count == 0) {
    return filter;
  }
  filter.bit_count = keys.size() * bits_per_key;
  filter.hash_count = hash_count;
  filter.bits.resize(BloomByteSize(filter.bit_count), 0);
  for (int key : keys) {
    filter.Add(key);
  }

  return filter;
}

// Bloom filter Add
// 하나의 키를 Bloom filter에 등록
// hash_count 개의 해시 함수로 생성한 위치에 모두 비트 설정
void BloomFilter::Add(int key) {
  if (bit_count == 0 || hash_count == 0 || bits.empty()) {
    return;
  }
  for (size_t i = 0; i < hash_count; ++i) {
    uint64_t hash_value = SimpleHash(key, i);
    size_t bit_index = PositiveModulo(hash_value, bit_count);
    SetBit(&bits, bit_index);
  }
}

// Bloom filter Query (MayContain)
// 주어진 키가 필터에 포함될 가능성을 확인
// 모든 해시 함수 위치의 비트가 모두 1이면 "포함될 수 있음" 반환
// 하나라도 0이면 "확실히 없음" 반환 (False Positive만 가능)
bool BloomFilter::MayContain(int key) const {
  if (bit_count == 0 || hash_count == 0 || bits.empty()) {
    return false;
  }

  for (size_t i = 0; i < hash_count; ++i) {
    uint64_t hash_value = SimpleHash(key, i);
    size_t bit_index = PositiveModulo(hash_value, bit_count);
    if (!GetBit(bits, bit_index)) {
      return false;
    }
  }

  return true;
}


// Bloom filter가 비어있는지 확인 (생성되지 않았거나 크기가 0인 경우)
bool BloomFilter::Empty() const { return bit_count == 0 || bits.empty(); }
