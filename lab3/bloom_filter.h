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

size_t BloomByteSize(size_t bit_count);
std::string EncodeBloomFilterBits(const std::vector<uint8_t>& bytes);
bool DecodeBloomFilterBits(const std::string& encoded,
                           std::vector<uint8_t>* out);
BloomFilter BuildBloomFilterFromKeys(const std::vector<int>& keys,
                                     size_t bits_per_key,
                                     size_t hash_count);

#endif  // LAB3_BLOOM_FILTER_H
