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

std::string EncodeBloomFilterBits(const std::vector<uint8_t>& bytes) {
  std::string out;

  // bloom filter bytes를 string으로 encoding하는 함수
  // 각 바이트를 2자리 16진수로 변환하여 문자열로 반환
  for (uint8_t byte : bytes) {
    out += NibbleToHex(byte >> 4);    // 상위 4비트
    out += NibbleToHex(byte & 0x0f);  // 하위 4비트
  }

  return out;
}

bool DecodeBloomFilterBits(const std::string& encoded,
                           std::vector<uint8_t>* out) {
  if (encoded.size() % 2 != 0) {
    return false;
  }

  // SSTable에 문자열로 저장된 Bloom filter bit array를 hex에서 decoding하는 함수
  // 2자리씩 읽어서 바이트로 변환
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

BloomFilter BuildBloomFilterFromKeys(const std::vector<int>& keys,
                                     size_t bits_per_key,
                                     size_t hash_count) {
  BloomFilter filter;
  if (keys.empty() || bits_per_key == 0 || hash_count == 0) {
    return filter;
  }

  // key 목록으로부터 Bloom filter를 생성하는 함수
  // 총 비트 수를 계산하고 BloomFilter 구조 초기화
  filter.bit_count = keys.size() * bits_per_key;
  filter.hash_count = hash_count;
  filter.bits.resize(BloomByteSize(filter.bit_count), 0);
  
  // 각 키를 Bloom filter에 추가
  for (int key : keys) {
    filter.Add(key);
  }

  return filter;
}
// 하나의 key를 bloom filter에 등록하는 함수
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

// 주어진 key에 대해 해당 bloom filter로 검사하는 함수
bool BloomFilter::MayContain(int key) const {
  // 모든 해시 함수에 대해 대응하는 비트가 모두 1이면 포함될 가능성이 있음
  if (bit_count == 0 || hash_count == 0 || bits.empty()) {
    return false;
  }

  for (size_t i = 0; i < hash_count; ++i) {
    uint64_t hash_value = SimpleHash(key, i);
    size_t bit_index = PositiveModulo(hash_value, bit_count);
    if (!GetBit(bits, bit_index)) {
      // 하나라도 0이면 확실히 없음
      return false;
    }
  }

  return true;
}

bool BloomFilter::Empty() const { return bit_count == 0 || bits.empty(); }
