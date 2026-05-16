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

BloomFilter BuildBloomFilter(const std::vector<SSTableEntry>& entries,
                             size_t bits_per_key, size_t hash_count) {
  std::vector<int> keys;
  keys.reserve(entries.size());
  for (const auto& entry : entries) keys.push_back(entry.key);
  return BuildBloomFilterFromKeys(keys, bits_per_key, hash_count);
}

bool ReadSSTableHeader(std::ifstream* in, SSTableHeader* header) {
  // Read META line: META <bloom_enabled> <smallest_key> <largest_key> <oldest_seq> <newest_seq>
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

  // If next line is BLOOM, parse it; otherwise rewind so the caller can read the first data line.
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
      // not a BLOOM line -> rewind to allow caller to read this data line
      in->clear();
      in->seekg(pos);
    }
  }
  return true;
}

bool ParseSSTableDataLine(const std::string& line, SSTableEntry* entry) {
  std::istringstream iss(line);
  std::string op;
  if (!(iss >> op)) return false;

  if (op == "P") {
    if (!(iss >> entry->key >> entry->seq)) return false;
    // remaining token(s) are the value (no spaces expected in tests)
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

// Public functions

void EnsureSSTDir(const std::string& sst_dir) {
  std::filesystem::create_directories(sst_dir);
}

bool IsSSTDirEmpty(const std::string& sst_dir) {
  EnsureSSTDir(sst_dir);
  return std::filesystem::directory_iterator(sst_dir) == std::filesystem::directory_iterator();
}

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

SSTableFile WriteSSTable(const std::string& sst_dir, uint64_t file_id,
                         const std::vector<SSTableEntry>& entries,
                         bool write_bloom_filter, size_t bloom_bits_per_key,
                         size_t bloom_hash_count) {
  if (entries.empty()) throw std::invalid_argument("cannot write empty SSTable");

  // Sort entries by key asc, seq desc
  std::vector<SSTableEntry> sorted = entries;
  std::sort(sorted.begin(), sorted.end(), [](const SSTableEntry& a, const SSTableEntry& b){
    if (a.key != b.key) return a.key < b.key;
    return a.seq > b.seq;
  });

  // Build bloom filter if requested
  BloomFilter bloom_filter;
  if (write_bloom_filter) bloom_filter = BuildBloomFilter(sorted, bloom_bits_per_key, bloom_hash_count);

  SSTableHeader header = BuildHeaderFromEntries(sorted, write_bloom_filter, bloom_filter);

  std::string filename = "sst_" + std::to_string(file_id) + ".txt";
  std::string filepath = std::filesystem::path(sst_dir) / filename;

  std::ofstream out(filepath);
  if (!out) throw std::runtime_error("failed to open SSTable file: " + filepath);

  // META line: bloom_enabled indicates whether BLOOM metadata is present.
  out << "META " << (write_bloom_filter ? 1 : 0) << " " << header.smallest_key << " " << header.largest_key << " "
      << header.oldest_seq << " " << header.newest_seq << "\n";

  if (write_bloom_filter) {
    out << "BLOOM " << header.bloom_filter.bit_count << " "
        << header.bloom_filter.hash_count << " "
        << EncodeBloomFilterBits(header.bloom_filter.bits) << "\n";
  }

  // Data lines
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

bool GetFromSSTable(const SSTableFile& file, int key, std::string* value,
                    bool* tombstone) {
  // Quick range check
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
