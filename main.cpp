#include <bits/stdc++.h>
#include <fcntl.h>
#include <filesystem>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "daily_writer.h"
#if defined(__x86_64__)
#include <immintrin.h>
#endif
using namespace std;
namespace fs = std::filesystem;
using ll = long long;
struct Hash {
  using is_transparent = void;
  size_t operator()(string_view s) const noexcept {
    return hash<string_view>{}(s);
  }
};
template <class V> using StrMap = unordered_map<string, V, Hash, equal_to<>>;
const ll DAY = 86400000, WEEK = 7 * DAY;
ll number(string_view s, ll cap = LLONG_MAX) {
  ll n = 0;
  for (char c : s) {
    if (c < '0' || c > '9' || n > (cap - (c - '0')) / 10)
      return -1;
    n = n * 10 + (c - '0');
  }
  return s.empty() ? -1 : n;
}
constexpr ll days(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = y - era * 400;
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return ll(era) * 146097 + (yo * 365 + yo / 4 - yo / 100 + doy) - 719468;
}
constexpr auto month_starts = [] {
  array<int, 10000 * 12 + 1> starts{};
  for (int i = 0; i < int(starts.size()); ++i)
    starts[i] = days(i / 12, i % 12 + 1, 1);
  return starts;
}();
constexpr ll bad_time = LLONG_MIN;
ll timestamp(string_view s) {
  if (s.size() < 5 || s[4] != '-') {
    if (!s.empty() && s.size() <= 18) {
      ll result = 0;
      for (unsigned char c : s) {
        unsigned digit = c - '0';
        if (digit > 9)
          return bad_time;
        result = 10 * result + digit;
      }
      return result;
    }
    ll epoch_ms = number(s);
    return epoch_ms < 0 ? bad_time : epoch_ms;
  }
  if ((s.size() != 20 && s.size() != 25) || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':')
    return bad_time;
  bool invalid_digits = false;
  auto pair_at = [&](int i) {
    unsigned a = static_cast<unsigned char>(s[i]) - '0';
    unsigned b = static_cast<unsigned char>(s[i + 1]) - '0';
    invalid_digits |= (a > 9) | (b > 9);
    return a * 10 + b;
  };
  unsigned century = pair_at(0), year = pair_at(2), m = pair_at(5), d = pair_at(8),
      h = pair_at(11), mi = pair_at(14), se = pair_at(17);
  if (invalid_digits || m < 1 || m > 12 || d < 1 || h > 23 || mi > 59 || se > 59)
    return bad_time;
  unsigned month = (century * 100 + year) * 12 + m - 1;
  int first = month_starts[month];
  if (int(d) > month_starts[month + 1] - first)
    return bad_time;
  ll off = 0;
  if (s.size() == 20) {
    if (s[19] != 'Z')
      return bad_time;
  } else {
    if ((s[19] != '+' && s[19] != '-') || s[22] != ':')
      return bad_time;
    unsigned oh = pair_at(20), om = pair_at(23);
    if (invalid_digits || oh > 23 || om > 59)
      return bad_time;
    off = ll(oh * 60 + om) * 60000 * (s[19] == '+' ? 1 : -1);
  }
  return (ll(first) + d - 1) * DAY + (h * 3600 + mi * 60 + se) * 1000 - off;
}
bool csv(string_view line, vector<string_view> &out, int count) {
  out.clear();
  size_t a = 0;
  for (;;) {
    auto b = line.find(',', a);
    if (b == string_view::npos)
      b = line.size();
    auto s = line.substr(a, b - a);
    if (!s.empty() && s[0] == '"') {
      if (s.size() < 2 || s.back() != '"')
        return false;
      s = s.substr(1, s.size() - 2);
    }
    if (s.find('"') != string_view::npos)
      return false;
    out.push_back(s);
    if (b == line.size())
      break;
    a = b + 1;
  }
  return out.size() == size_t(count);
}
vector<fs::path> csv_files(const fs::path &p) {
  if (!fs::is_directory(p))
    return {p};
  vector<fs::path> files;
  for (auto &e : fs::directory_iterator(p))
    if (e.path().extension() == ".csv")
      files.push_back(e.path());
  sort(files.begin(), files.end());
  return files;
}

// Read a disjoint range of line starts. A preceding newline belongs to the
// previous range; an unterminated last line is still a record.
template <class F>
void read_stream_range(const fs::path &file, uint64_t begin, uint64_t end, int fields,
                F fun) {
  vector<char> buffer(256 * 1024);
  ifstream in;
  in.rdbuf()->pubsetbuf(buffer.data(), buffer.size());
  in.open(file, ios::binary);
  if (!in)
    throw runtime_error("cannot open " + file.string());
  uint64_t pos = begin ? begin - 1 : 0;
  in.seekg(pos);
  string line;
  getline(in, line); // Skip the header or the tail of the preceding record.
  pos += line.size() + 1;
  vector<string_view> values;
  values.reserve(fields);
  while (pos < end && getline(in, line)) {
    pos += line.size() + 1;
    if (csv(line, values, fields))
      fun(values);
  }
  if (in.bad())
    throw runtime_error("read failed: " + file.string());
}
#if defined(__x86_64__)
// Decode all complete rows in a block. The next block starts at the first
// unconsumed row, so delimiter offsets fit in sixteen bits.
constexpr int csv_block = 2048;
__attribute__((target("avx2,popcnt,pclmul")))
static bool index_block(const char *data, uint16_t *cuts, uint16_t *rows,
                        int &row_count) {
  int fields = 0;
  uint32_t preceding_cut = 1, pending_quote = 0, parity_carry = 0, bad = 0;
  for (int base = 0; base < csv_block; base += 32) {
    const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + base));
    const auto lf = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8('\n'));
    const auto comma = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(','));
    uint32_t ends = _mm256_movemask_epi8(_mm256_or_si256(lf, comma));
    uint32_t lines = _mm256_movemask_epi8(lf);
    uint32_t marks = _mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, _mm256_set1_epi8('"')));
    // Each quote must touch a delimiter. A quote in the final lane can
    // also be adjacent to the delimiter in lane zero of the next vector.
    uint32_t interior = marks & ~((ends << 1) | preceding_cut | (ends >> 1));
    bad |= (interior & 0x7fffffffU) | (pending_quote & ~ends);
    pending_quote = interior >> 31;
    preceding_cut = ends >> 31;

    // Carryless multiplication by all ones computes the inclusive prefix XOR
    // of quote bits. Every comma and LF must end a field with even parity.
    const auto prefix = _mm_clmulepi64_si128(_mm_cvtsi32_si128(marks), _mm_set1_epi32(-1), 0);
    uint32_t parity = uint32_t(_mm_cvtsi128_si32(prefix)) ^ parity_carry;
    bad |= parity & ends;
    parity_carry = 0u - (parity >> 31);
    while (lines) {
      uint32_t through_lf = lines ^ (lines - 1);
      rows[row_count++] = fields + popcount(ends & through_lf);
      lines &= lines - 1;
    }
    while (ends) {
      cuts[++fields] = uint16_t(base + countr_zero(ends));
      ends &= ends - 1;
    }
  }
  // Any trailing partial row is indexed again starting at its own boundary.
  return bad == 0;
}

template <int Fields, class F>
const char *read_blocks(const char *line, const char *limit, F &fun) {
  array<uint16_t, csv_block + 1> cuts;
  array<uint16_t, csv_block> rows;
  array<string_view, Fields> values;
  vector<string_view> scalar;
  while (limit - line >= csv_block) {
    // The synthetic cut immediately before the block is its preceding LF.
    cuts[0] = UINT16_MAX;
    int row_count = 0;
    bool clean = index_block(line, cuts.data(), rows.data(), row_count);
    if (row_count == 0)
      break;
    int first = 0;
    for (int i = 0; i < row_count; ++i) {
      const int last = rows[i];
      if (last - first == Fields) {
        if (clean) {
          for (int f = 0; f < Fields; ++f) {
            const char *start = line + uint16_t(cuts[first + f] + 1);
            const char *end = line + cuts[first + f + 1];
            int trim = *start == '"';
            values[f] = {start + trim, size_t(end - start - 2 * trim)};
          }
          fun(values);
        } else {
          const char *start = line + uint16_t(cuts[first] + 1);
          if (csv({start, size_t(line + cuts[last] - start)}, scalar, Fields))
            fun(scalar);
        }
      }
      first = last;
    }
    line += cuts[first] + 1;
  }
  return line;
}
#endif

template <int Fields, class F>
void read_range(const fs::path &file, uint64_t begin, uint64_t end,
                F fun) {
  const int fd = open(file.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd == -1)
    throw system_error(errno, generic_category(), file.string());
  struct stat info;
  if (fstat(fd, &info) == -1) {
    const int error = errno;
    close(fd);
    throw system_error(error, generic_category(), file.string());
  }
  const size_t size = info.st_size;
  if (begin >= size) {
    close(fd);
    return;
  }
  void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    read_stream_range(file, begin, end, Fields, move(fun));
    return;
  }
  auto release = [size](void *address) { munmap(address, size); };
  const unique_ptr<void, decltype(release)> mapping(mapped, release);
  const auto *data = static_cast<const char *>(mapping.get());
  const auto *file_end = data + size;
  const auto *range_end = data + min<uint64_t>(end, size);
  auto newline = [file_end](const char *line) {
    return static_cast<const char *>(memchr(line, '\n', file_end - line));
  };
  const auto *previous = newline(data + (begin ? begin - 1 : 0));
  if (!previous)
    return;
  vector<string_view> values;
  values.reserve(Fields);
  for (auto *line = previous + 1; line < range_end;) {
#if defined(__x86_64__)
    if (range_end - line >= csv_block && __builtin_cpu_supports("avx2") &&
        __builtin_cpu_supports("popcnt") && __builtin_cpu_supports("pclmul")) {
      line = read_blocks<Fields>(line, range_end, fun);
      if (line == range_end)
        break;
    }
#endif
    const auto *next = newline(line);
    const auto *line_end = next ? next : file_end;
    if (csv(string_view(line, line_end - line), values, Fields))
      fun(values);
    line = next ? next + 1 : file_end;
  }
}
template <int Fields, class F> void rows(const fs::path &p, F fun) {
  for (const auto &file : csv_files(p))
    read_range<Fields>(file, 0, fs::file_size(file), fun);
}

template <class F> void parallel_jobs(size_t jobs, int threads, F fun) {
  atomic<size_t> next{0};
  vector<thread> pool;
  for (int i = 0; i < threads && size_t(i) < jobs; ++i)
    pool.emplace_back([&, i] {
      for (;;) {
        size_t j = next.fetch_add(1, memory_order_relaxed);
        if (j >= jobs)
          break;
        fun(i, j);
      }
    });
  for (auto &worker : pool)
    worker.join();
}
struct Range {
  fs::path file;
  uint64_t begin, end;
};
vector<Range> line_ranges(const fs::path &dir, uint64_t block = 8 << 20) {
  vector<Range> ranges;
  for (const auto &file : csv_files(dir)) {
    uint64_t bytes = fs::file_size(file);
    for (uint64_t pos = 0; pos < bytes; pos += block)
      ranges.push_back({file, pos, min(pos + block, bytes)});
  }
  return ranges;
}
constexpr size_t person_groups = 128;
template <class T> using Groups = array<vector<T>, person_groups>;

template <class T>
vector<T> gather(vector<Groups<T>> &sources, size_t group) {
  size_t total = 0;
  for (const auto &source : sources)
    total += source[group].size();
  vector<T> result;
  result.reserve(total);
  for (auto &source : sources) {
    auto &batch = source[group];
    result.insert(result.end(), make_move_iterator(batch.begin()),
                  make_move_iterator(batch.end()));
    vector<T>().swap(batch);
  }
  return result;
}
struct Strings {
  StrMap<int> ids;
  vector<string> names;
  int id(string_view s) {
    auto it = ids.find(s);
    if (it != ids.end())
      return it->second;
    int n = names.size();
    names.emplace_back(s);
    ids.emplace(names.back(), n);
    return n;
  }
} cats, camps;

class UserIndex {
  struct Slot {
    uint64_t key = 0;
    int id = -1;
  };
  vector<Slot> slots = vector<Slot>(16);
  size_t occupied = 0;
  StrMap<int> other;

  // All nine-byte IDs beginning with 'u' have an exact fixed-width encoding.
  // Every other ID keeps its complete string and goes through the fallback.
  static bool packed_key(string_view name, uint64_t &key) {
    if (name.size() != 9 || name.front() != 'u')
      return false;
    memcpy(&key, name.data() + 1, sizeof(key));
    return true;
  }

  static size_t home(uint64_t key, size_t mask) {
    uint64_t mixed = (key ^ (key >> 33)) * 0xff51afd7ed558ccdULL;
    return (mixed ^ (mixed >> 33)) & mask;
  }

  static size_t probe(const vector<Slot> &table, uint64_t key, size_t index) {
    const size_t mask = table.size() - 1;
    while (table[index].id >= 0 && table[index].key != key)
      index = (index + 1) & mask;
    return index;
  }

  static size_t probe(const vector<Slot> &table, uint64_t key) {
    return probe(table, key, home(key, table.size() - 1));
  }

public:
  struct Lookup {
    uint64_t key = 0;
    size_t first = SIZE_MAX;
  };

  vector<string> names;

  void apply_roots(const vector<int> &roots) {
    for (auto &slot : slots)
      if (slot.id >= 0)
        slot.id = roots[slot.id];
    for (auto &entry : other)
      entry.second = roots[entry.second];
  }

  Lookup prepare_lookup(string_view name) const {
    Lookup lookup;
    if (packed_key(name, lookup.key)) {
      lookup.first = home(lookup.key, slots.size() - 1);
      __builtin_prefetch(&slots[lookup.first], 0, 2);
    }
    return lookup;
  }

  int find(string_view name, Lookup lookup) const {
    if (lookup.first != SIZE_MAX)
      return slots[probe(slots, lookup.key, lookup.first)].id;
    const auto item = other.find(name);
    return item == other.end() ? -1 : item->second;
  }

  int find(string_view name) const {
    Lookup lookup;
    if (packed_key(name, lookup.key))
      lookup.first = home(lookup.key, slots.size() - 1);
    return find(name, lookup);
  }

  int id(string_view name) {
    uint64_t key;
    if (packed_key(name, key)) {
      auto index = probe(slots, key);
      if (slots[index].id >= 0)
        return slots[index].id;
      if ((occupied + 1) * 4 > slots.size() * 3) {
        vector<Slot> larger(slots.size() * 2);
        for (const auto &slot : slots)
          if (slot.id >= 0)
            larger[probe(larger, slot.key)] = slot;
        slots.swap(larger);
        index = probe(slots, key);
      }
      const int index_id = names.size();
      names.emplace_back(name);
      slots[index] = {key, index_id};
      ++occupied;
      return index_id;
    }
    const auto item = other.find(name);
    if (item != other.end())
      return item->second;
    const int index_id = names.size();
    names.emplace_back(name);
    other.emplace(names.back(), index_id);
    return index_id;
  }

  void build_links(const fs::path &dir, int threads) {
    struct Batch {
      vector<array<uint64_t, 2>> packed;
      vector<pair<string, string>> other;
    };
    auto ranges = line_ranges(dir, 1 << 20);
    vector<Batch> batches(ranges.size());
    parallel_jobs(ranges.size(), threads, [&](int, size_t r) {
      auto &batch = batches[r];
      auto &range = ranges[r];
      read_range<2>(range.file, range.begin, range.end, [&](auto &f) {
        if (f[0].empty() || f[1].empty())
          return;
        array<uint64_t, 2> edge;
        if (packed_key(f[0], edge[0]) && edge[0] &&
            packed_key(f[1], edge[1]) && edge[1])
          batch.packed.push_back(edge);
        else
          batch.other.emplace_back(f[0], f[1]);
      });
    });

    size_t endpoints = 0;
    bool has_other = false;
    for (const auto &batch : batches) {
      endpoints += 2 * batch.packed.size();
      has_other |= !batch.other.empty();
    }
    size_t capacity = slots.size();
    while (endpoints > capacity / 4 * 3)
      capacity *= 2;
    slots.resize(capacity);

    // A nonzero suffix is its own publication token. No separate value is
    // read until all inserters have joined. Zero suffixes follow the merge below.
    parallel_jobs(batches.size(), threads, [&](int, size_t r) {
      for (auto &edge : batches[r].packed) {
        for (auto &key : edge) {
          size_t pos = home(key, capacity - 1);
          for (;; pos = (pos + 1) & (capacity - 1)) {
            atomic_ref<uint64_t> cell(slots[pos].key);
            uint64_t seen = cell.load(memory_order_relaxed);
            if (!seen)
              cell.compare_exchange_strong(seen, key, memory_order_relaxed);
            if (!seen || seen == key)
              break;
          }
          key = pos;
        }
      }
    });

    vector<int> parent(capacity, -1);
    // Every successful hook points strictly toward a smaller byte string.
    // Concurrent unions can move the target root again without creating a cycle.
    auto concurrent_root = [&](int pos) {
      for (;;) {
        int next = atomic_ref<int>(parent[pos]).load(memory_order_relaxed);
        if (next < 0)
          return pos;
        int grandparent = atomic_ref<int>(parent[next]).load(memory_order_relaxed);
        if (grandparent >= 0) {
          atomic_ref<int>(parent[pos]).store(grandparent, memory_order_relaxed);
          pos = grandparent;
        } else {
          pos = next;
        }
      }
    };
    parallel_jobs(batches.size(), threads, [&](int, size_t r) {
      for (const auto &edge : batches[r].packed) {
        int a = edge[0], b = edge[1];
        for (;;) {
          a = concurrent_root(a);
          b = concurrent_root(b);
          if (a == b)
            break;
          if (memcmp(&slots[b].key, &slots[a].key, 8) < 0)
            swap(a, b);
          int expected = -1;
          if (atomic_ref<int>(parent[b]).compare_exchange_strong(
                  expected, a, memory_order_relaxed))
            break;
          b = expected;
        }
      }
    });

    // Partition the slot array, count component roots and prefix-sum their
    // counts. This gives each job a disjoint slice of the canonical names.
    constexpr size_t block_size = 32768;
    const size_t blocks = (capacity + block_size - 1) / block_size;
    struct alignas(64) Counts { size_t keys = 0, roots = 0, next = 0; };
    vector<Counts> counts(blocks);
    auto scan = [&](auto visit) {
      parallel_jobs(blocks, threads, [&](int, size_t job) {
        const size_t end = min(capacity, (job + 1) * block_size);
        for (size_t i = job * block_size; i < end; ++i)
          if (slots[i].key)
            visit(job, i);
      });
    };
    scan([&](size_t job, size_t i) {
      ++counts[job].keys;
      counts[job].roots += parent[i] == -1;
    });
    size_t roots = 0;
    for (auto &block : counts) {
      occupied += block.keys;
      block.next = roots;
      roots += block.roots;
    }
    names.resize(roots);
    scan([&](size_t job, size_t i) {
      if (parent[i] == -1) {
        const int id = counts[job].next++;
        char name[9] = {'u'};
        memcpy(name + 1, &slots[i].key, 8);
        names[id].assign(name, sizeof(name));
        parent[i] = -2 - id;
      }
    });
    scan([&](size_t, size_t i) {
      int r = concurrent_root(i);
      slots[i].id = -2 - atomic_ref<int>(parent[r]).load(memory_order_relaxed);
    });

    if (!has_other)
      return;
    // Mixed edges can join entire packed components. id() already returns
    // their canonical vertex, and interns any new endpoint by its full bytes.
    vector<int> merged;
    auto representative = [&](int vertex) {
      while (merged.size() < names.size())
        merged.push_back(merged.size());
      while (merged[vertex] != vertex) {
        merged[vertex] = merged[merged[vertex]];
        vertex = merged[vertex];
      }
      return vertex;
    };
    for (const auto &batch : batches) {
      for (const auto &[left, right] : batch.other) {
        int a = representative(id(left)), b = representative(id(right));
        if (a != b) {
          if (names[b] < names[a])
            swap(a, b);
          merged[b] = a;
        }
      }
    }
    for (size_t i = 0; i < merged.size(); ++i)
      merged[i] = representative(i);
    apply_roots(merged);
  }
} users;

struct PackedPurchase {
  uint32_t price : 24 = 0, cat : 8 = 0;
};
struct WidePurchase {
  uint32_t price = 0, cat = 0;
};
template <class Time, class Purchase> struct EventRecord {
  Time ts = 0;
  int user = -1;
  Purchase purchase;
};

// A person group fixes the low bits of the user ID. Count only the user
// slots belonging to this group, then scatter straight from the dedup batches.
template <class Event>
struct CompactEventOrder {
  vector<size_t> ends;
  vector<Event> ordered, scratch;

  void order_times(span<Event> events) {
    if (events.size() < 2)
      return;
    if (events.size() < 512) {
      sort(events.begin(), events.end(),
           [](const Event &a, const Event &b) { return a.ts < b.ts; });
      return;
    }
    // Count all four bytes in one traversal. A constant byte needs no pass.
    size_t counts[4][256]{};
    for (const auto &event : events)
      for (unsigned pass = 0; pass < 4; ++pass)
        ++counts[pass][(event.ts >> (8 * pass)) & 255];
    if (scratch.size() < events.size())
      scratch.resize(events.size());
    Event *src = events.data(), *dst = scratch.data();
    for (unsigned pass = 0; pass < 4; ++pass) {
      auto &offsets = counts[pass];
      if (offsets[(src[0].ts >> (8 * pass)) & 255] == events.size())
        continue;
      exclusive_scan(begin(offsets), end(offsets), begin(offsets), size_t{0});
      for (size_t i = 0; i < events.size(); ++i) {
        auto digit = (src[i].ts >> (8 * pass)) & 255;
        dst[offsets[digit]++] = src[i];
      }
      swap(src, dst);
    }
    if (src != events.data())
      copy_n(src, events.size(), events.data());
  }

  span<Event> run(vector<Groups<Event>> &sources, size_t group, size_t users) {
    size_t count = 0;
    for (const auto &source : sources)
      count += source[group].size();
    size_t slots = group < users ? 1 + (users - 1 - group) / person_groups : 0;
    if (count < 256 || slots > count) {
      ordered = gather(sources, group);
      sort(ordered.begin(), ordered.end(), [](const Event &a, const Event &b) {
        auto key = [](const Event &e) { return (uint64_t(e.user) << 32) | e.ts; };
        return key(a) < key(b);
      });
      return ordered;
    }
    ends.assign(slots, 0);
    for (const auto &source : sources)
      for (const auto &event : source[group])
        ++ends[event.user / person_groups];
    exclusive_scan(ends.begin(), ends.end(), ends.begin(), size_t{0});
    ordered.resize(count);
    for (auto &source : sources) {
      for (const auto &event : source[group])
        ordered[ends[event.user / person_groups]++] = event;
      vector<Event>().swap(source[group]);
    }
    size_t first = 0;
    for (auto last : ends) {
      order_times(span(ordered).subspan(first, last - first));
      first = last;
    }
    return ordered;
  }
};
// Common event IDs have prefix e and up to eight suffix bytes. Store the
// complete suffix in the key. Zero bytes and a set high bit take the exact
// string path so the two key spaces cannot overlap.
class EventKeys {
  mutex guard;
  StrMap<uint64_t> other;
public:
  uint64_t get(string_view s) {
    constexpr uint64_t tag = uint64_t(1) << 63;
    if (s.front() == 'e' && s.size() == 9) {
      uint64_t bits;
      memcpy(&bits, s.data() + 1, sizeof(bits));
      // A zero suffix byte requires the length-aware string fallback.
      constexpr uint64_t ones = 0x0101010101010101ULL;
      const bool has_zero = ((bits - ones) & ~bits & (ones << 7)) != 0;
      if (!(bits & tag) && !has_zero)
        return bits;
    } else if (s.front() == 'e' && s.size() < 9) {
      auto suffix = s.substr(1);
      uint64_t bits = 0;
      memcpy(&bits, suffix.data(), suffix.size());
      if (!(bits & tag) && suffix.find(char(0)) == string_view::npos)
        return bits;
    }
    lock_guard lock(guard);
    auto it = other.find(s);
    if (it != other.end())
      return it->second;
    return other.emplace(string(s), tag | other.size()).first->second;
  }
};
template <class Event> struct KeptRecord {
  uint64_t key;
  uint32_t ingest;
  Event e;
};
using CompactEvent = EventRecord<uint32_t, PackedPurchase>;
static_assert(sizeof(CompactEvent) == 12 && sizeof(KeptRecord<CompactEvent>) == 24);

// Common codes are monotone UTC seconds centered on the window start. Reserve
// one code for exact millisecond values, including the parser's bad_time.
struct IngestClock {
  static constexpr uint32_t exceptional = UINT32_MAX;
  ll first_second;
  explicit IngestClock(ll start) : first_second(start / 1000 - INT32_MAX) {}

  uint32_t encode(ll ms) const {
    ll seconds = ms / 1000;
    uint64_t offset = uint64_t(seconds - first_second);
    return ms % 1000 == 0 && offset < exceptional ? uint32_t(offset) : exceptional;
  }
  ll decode(uint32_t offset) const { return (first_second + offset) * 1000; }
};

class ClickIds {
  static constexpr uint64_t fallback = uint64_t{1} << 63;
  vector<vector<string>> other_ids;

  static uint64_t byte_order(uint64_t value) {
    if constexpr (endian::native == endian::little)
      return __builtin_bswap64(value);
    return value;
  }

  string_view bytes(uint64_t key, array<char, 9> &buffer) const {
    if (key & fallback) {
      uint64_t index = key & ~fallback;
      return other_ids[index % other_ids.size()][index / other_ids.size()];
    }
    buffer[0] = 'k';
    uint64_t suffix = byte_order(key);
    memcpy(buffer.data() + 1, &suffix, sizeof(suffix));
    return {buffer.data(), buffer.size()};
  }

public:
  explicit ClickIds(int readers) : other_ids(readers) {}

  uint64_t encode(string_view id, int reader) {
    if (id.size() == 9 && id.front() == 'k') {
      uint64_t suffix;
      memcpy(&suffix, id.data() + 1, sizeof(suffix));
      const auto key = byte_order(suffix);
      if (!(key & fallback))
        return key;
    }
    // Each worker owns one fallback vector until the reader threads join.
    auto &ids = other_ids[reader];
    uint64_t index = ids.size() * other_ids.size() + reader;
    ids.emplace_back(id);
    return fallback | index;
  }

  strong_ordering compare(uint64_t a, uint64_t b) const {
    if (a == b || !((a | b) & fallback))
      return a <=> b;
    array<char, 9> a_buffer, b_buffer;
    return bytes(a, a_buffer) <=> bytes(b, b_buffer);
  }
};

struct Click {
  ll ts;
  uint64_t id;
  int user, camp;
};
struct Agg {
  ll sessions = 0, events = 0, revenue = 0;
  map<int, ll> cat;
};

struct CampaignAgg {
  ll sessions = 0, revenue = 0, purchasing = 0, assist = 0;
};

ll utc_day(ll timestamp) { return timestamp / DAY - (timestamp % DAY < 0); }

class CampaignTotals {
  ll first_day;
  size_t days;
  vector<CampaignAgg> dense;
  map<pair<int, ll>, CampaignAgg> sparse;

public:
  CampaignTotals(size_t campaigns, ll first, ll last)
      : first_day(first), days(last - first + 1) {
    // Avoid scaling allocations with a disproportionately wide date window.
    constexpr size_t max_dense_entries = 131072;
    if (campaigns <= max_dense_entries / days)
      dense.resize(campaigns * days);
  }

  CampaignAgg &at(int campaign, ll day) {
    if (dense.empty())
      return sparse[{campaign, day}];
    return dense[size_t(campaign) * days + (day - first_day)];
  }

  template <class F> void for_each(F combine) const {
    if (dense.empty()) {
      for (const auto &[key, total] : sparse)
        combine(key, total);
      return;
    }
    for (size_t i = 0; i < dense.size(); ++i) {
      const auto &total = dense[i];
      if (total.sessions || total.assist)
        combine(pair{int(i / days), first_day + ll(i % days)}, total);
    }
  }
};

fs::path part_name(const fs::path &folder, int worker) {
  return folder / (worker ? "part-" + to_string(worker) + ".parquet" : "part.parquet");
}

void finish_parts(const fs::path &folder, int count) {
  set<fs::path> current;
  for (int i = 0; i < count; ++i) {
    auto path = part_name(folder, i);
    fs::rename(path.string() + ".tmp", path);
    current.insert(path);
  }
  // A retry may use fewer workers or an output folder from a prior run.
  for (const auto &e : fs::directory_iterator(folder)) {
    auto name = e.path().filename().string();
    bool abandoned = name.starts_with("part-") && name.ends_with(".parquet.tmp");
    if (e.is_regular_file() && (abandoned ||
        (e.path().extension() == ".parquet" && !current.contains(e.path()))))
      fs::remove(e.path());
  }
}
template <class Time, class Purchase>
void process(const string &data, const string &out, ll start, ll end,
             chrono::steady_clock::time_point begin, const StrMap<int> &products, int unknown) {
  using Event = EventRecord<Time, Purchase>;
  using Kept = KeptRecord<Event>;
  const IngestClock clock(start);
  // Valid events have already passed the half-open window check on absolute time.
  const ll origin = is_same_v<Time, uint32_t> ? start : 0;
  auto checkpoint = [&](const char *label) {
    cerr
        << label << " seconds "
        << chrono::duration<double>(chrono::steady_clock::now() - begin).count()
        << "\n";
  };
  const int threads = clamp(getenv("NCPU") ? atoi(getenv("NCPU")) : 8, 1, 8);
  users.build_links(data + "/links", threads);
  vector<string> channels;
  int organic = camps.id("organic");
  channels.push_back("organic");
  rows<2>(data + "/campaigns.csv", [&](auto &f) {
    int c = camps.id(f[0]);
    if (channels.size() <= size_t(c))
      channels.resize(c + 1);
    channels[c] = f[1];
  });
  checkpoint("links and dimensions");
  constexpr size_t partitions = 256;
  struct Reader {
    array<vector<Kept>, partitions> records;
    // Indices stay valid as the record vectors grow during parsing.
    array<vector<pair<size_t, ll>>, partitions> exceptions;
    UserIndex unlinked;
    vector<int> resolved;
  };
  auto ranges = line_ranges(data + "/events");
  vector<Reader> readers(threads);
  EventKeys keys;
  parallel_jobs(ranges.size(), threads, [&](int worker, size_t job) {
    auto &reader = readers[worker];
    const auto &range = ranges[job];
    read_range<7>(range.file, range.begin, range.end, [&](auto &f) {
      if (f[0].empty())
        return;
      const auto user_lookup = users.prepare_lookup(f[1]);
      ll ingest = timestamp(f[3]);
      ll ts = timestamp(f[2]);
      bool purchase = f[4] == "purchase";
      ll price = purchase ? number(f[6], 10000000) : 0;
      Event e;
      if (!f[1].empty() && ts >= start && ts < end &&
          (purchase || f[4] == "view" || f[4] == "cart") &&
          !f[5].empty() && (!purchase || price >= 1) &&
          (ingest <= ts || uint64_t(ingest) - uint64_t(ts) <= uint64_t(DAY))) {
        const int known = users.find(f[1], user_lookup);
        e.user = known < 0 ? -2 - reader.unlinked.id(f[1]) : known;
        e.ts = static_cast<Time>(ts - origin);
        if (purchase) {
          auto product = products.find(f[5]);
          e.purchase.price = price;
          e.purchase.cat = product == products.end() ? unknown : product->second;
        }
      }
      // Invalid but well-formed copies still participate in deduplication.
      auto key = keys.get(f[0]);
      // SplitMix64 finalizer spreads both packed and interned IDs across readers'
      // buckets. Keep the exact key for equality inside each bucket.
      uint64_t h = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
      h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
      auto part = (h ^ (h >> 31)) % partitions;
      auto &batch = reader.records[part];
      uint32_t code = clock.encode(ingest);
      if (code == IngestClock::exceptional)
        reader.exceptions[part].emplace_back(batch.size(), ingest);
      batch.push_back({key, code, e});
    });
  });
  // Establish IDs for one reader, then all remaining readers can resolve
  // overlaps concurrently while the global index is immutable.
  auto seed = max_element(readers.begin(), readers.end(), [](const Reader &a, const Reader &b) {
    return a.unlinked.names.size() < b.unlinked.names.size();
  });
  seed->resolved.reserve(seed->unlinked.names.size());
  for (const auto &name : seed->unlinked.names)
    seed->resolved.push_back(users.id(name));
  seed->unlinked = {};
  parallel_jobs(readers.size(), threads, [&](int, size_t i) {
    auto &reader = readers[i];
    reader.resolved.reserve(reader.unlinked.names.size());
    for (const auto &name : reader.unlinked.names)
      reader.resolved.push_back(users.find(name));
  });
  for (auto &reader : readers) {
    for (size_t i = 0; i < reader.unlinked.names.size(); ++i)
      if (reader.resolved[i] < 0)
        reader.resolved[i] = users.id(reader.unlinked.names[i]);
    reader.unlinked = {};
  }
  checkpoint("event parse");
  vector<Groups<Event>> surviving(threads);
  parallel_jobs(partitions, threads, [&](int worker, size_t part) {
    size_t count = 0;
    for (auto &reader : readers)
      count += reader.records[part].size();
    if (count == 0)
      return;
    // Batches now have stable addresses. Only exceptional duplicate comparisons
    // need this table. The normal case compares the two 32-bit codes directly.
    unordered_map<const Kept *, ll> exact;
    for (auto &reader : readers)
      for (auto [index, ms] : reader.exceptions[part])
        exact.emplace(&reader.records[part][index], ms);
    auto earlier = [&](const Kept &a, const Kept &b) {
      if (a.ingest != IngestClock::exceptional && b.ingest != IngestClock::exceptional)
        return a.ingest < b.ingest;
      auto millis = [&](const Kept &r) {
        return r.ingest == IngestClock::exceptional ? exact.at(&r) : clock.decode(r.ingest);
      };
      return millis(a) < millis(b);
    };
    // Each slot points into an immutable-size reader batch. On a duplicate
    // invalidate the loser, even when the winning copy is itself invalid.
    const size_t capacity = bit_ceil(count + count / 3 + 1);
    vector<Kept *> slots(capacity);
    for (auto &reader : readers) {
      for (auto &record : reader.records[part]) {
        uint64_t h = (record.key ^ (record.key >> 33)) * 0xff51afd7ed558ccdULL;
        size_t i = (h ^ (h >> 33)) & (capacity - 1);
        while (slots[i] && slots[i]->key != record.key)
          i = (i + 1) & (capacity - 1);
        Kept *&winner = slots[i];
        if (!winner) {
          winner = &record;
        } else if (earlier(record, *winner)) {
          winner->e.user = -1;
          winner = &record;
        } else {
          record.e.user = -1;
        }
      }
    }
    auto &groups = surviving[worker];
    for (auto &reader : readers) {
      auto &batch = reader.records[part];
      for (auto &record : batch) {
        auto &e = record.e;
        if (e.user == -1)
          continue;
        if (e.user < -1)
          e.user = reader.resolved[-2 - e.user];
        groups[e.user % person_groups].push_back(e);
      }
      vector<Kept>().swap(batch);
    }
  });
  checkpoint("event dedup");
  vector<Groups<Click>> click_batches(threads);
  ClickIds click_ids(threads);
  ranges = line_ranges(data + "/clicks");
  parallel_jobs(ranges.size(), threads, [&](int worker, size_t job) {
    const auto &range = ranges[job];
    read_range<4>(range.file, range.begin, range.end, [&](auto &f) {
      if (f[0].empty() || f[1].empty())
        return;
      const auto user_lookup = users.prepare_lookup(f[1]);
      ll ts = timestamp(f[3]);
      auto c = as_const(camps.ids).find(f[2]);
      if (ts < start - WEEK || ts >= end || c == camps.ids.end() ||
          c->second == organic)
        return;
      // A previously unseen, unlinked user has no events to credit or assist.
      const int person = users.find(f[1], user_lookup);
      if (person < 0)
        return;
      click_batches[worker][person % person_groups].push_back(
          {ts, click_ids.encode(f[0], worker), person, c->second});
    });
  });
  checkpoint("click parse");
  vector<CampaignTotals> totals;
  totals.reserve(threads);
  for (int i = 0; i < threads; ++i)
    totals.emplace_back(camps.names.size(), utc_day(start), utc_day(end - 1));
  fs::create_directories(out + "/user_daily");
  fs::create_directories(out + "/campaign_daily");
  vector<unique_ptr<DailyWriter>> user_files;
  for (int i = 0; i < threads; ++i)
    user_files.push_back(make_unique<DailyWriter>(
        part_name(out + "/user_daily", i).string() + ".tmp", true));
  atomic<size_t> valid_events{0};
  vector<CompactEventOrder<Event>> compact_order(threads);
  parallel_jobs(person_groups, threads, [&](int worker, size_t group) {
    auto events = [&] {
      if constexpr (is_same_v<Time, uint32_t>)
        return compact_order[worker].run(surviving, group, users.names.size());
      else {
        auto batch = gather(surviving, group);
        sort(batch.begin(), batch.end(), [](const Event &a, const Event &b) {
          return tie(a.user, a.ts) < tie(b.user, b.ts);
        });
        return batch;
      }
    }();
    auto clicks = gather(click_batches, group);
    valid_events.fetch_add(events.size(), memory_order_relaxed);
    sort(clicks.begin(), clicks.end(), [&](auto &a, auto &b) {
      if (a.user != b.user)
        return a.user < b.user;
      if (a.ts != b.ts)
        return a.ts < b.ts;
      return click_ids.compare(a.id, b.id) > 0;
    });
    // Identical copies are adjacent. Descending ID leaves the smallest tie last.
    clicks.erase(unique(clicks.begin(), clicks.end(), [&](auto &a, auto &b) {
      return click_ids.compare(a.id, b.id) == 0;
    }), clicks.end());
    auto &ca = totals[worker];
    auto &user_file = *user_files[worker];
    size_t ei = 0, ci = 0;
    while (ei < events.size()) {
      int user = events[ei].user;
      size_t ee = ei;
      while (ee < events.size() && events[ee].user == user)
        ee++;
      while (ci < clicks.size() && clicks[ci].user < user)
        ci++;
      size_t ce = ci;
      while (ce < clicks.size() && clicks[ce].user == user)
        ce++;
      size_t left = ci, right = ci;
      unordered_map<int, int> active;
      map<ll, Agg> ua;
      while (ei < ee) {
        ll sessionStart = origin + ll(events[ei].ts), day = utc_day(sessionStart);
        while (right < ce && clicks[right].ts <= sessionStart)
          active[clicks[right++].camp]++;
        while (left < right && clicks[left].ts < sessionStart - WEEK) {
          int camp = clicks[left++].camp;
          if (--active[camp] == 0)
            active.erase(camp);
        }
        int credit = right > left ? clicks[right - 1].camp : organic;
        auto &us = ua[day];
        auto &cs = ca.at(credit, day);
        us.sessions++;
        cs.sessions++;
        bool purchase = false;
        Time prev;
        do {
          auto &e = events[ei++];
          us.events++;
          us.revenue += e.purchase.price;
          cs.revenue += e.purchase.price;
          if (e.purchase.price) {
            purchase = true;
            us.cat[e.purchase.cat] += e.purchase.price;
          }
          prev = e.ts;
        } while (ei < ee && uint64_t(events[ei].ts) - uint64_t(prev) <= 1800000);
        cs.purchasing += purchase;
        for (auto &[camp, n] : active)
          ca.at(camp, day).assist++;
      }
      for (auto &[day, a] : ua) {
        int top = -1;
        ll revenue = -1;
        for (auto &[cat, r] : a.cat)
          if (r > revenue ||
              (r == revenue && cats.names[cat] < cats.names[top])) {
            top = cat;
            revenue = r;
          }
        user_file.append(users.names[user], day, a.sessions, a.events, a.revenue,
                         top < 0 ? nullptr : &cats.names[top]);
      }
      ci = ce;
    }
  });
  parallel_jobs(threads, threads, [&](int, size_t i) { user_files[i]->close(); });
  cerr << "valid events " << valid_events.load() << "\n";
  map<pair<int, ll>, CampaignAgg> ca;
  for (const auto &local : totals) {
    local.for_each([&](auto key, const CampaignAgg &a) {
      auto &total = ca[key];
      total.sessions += a.sessions;
      total.purchasing += a.purchasing;
      total.revenue += a.revenue;
      total.assist += a.assist;
    });
  }
  DailyWriter campaign_file(part_name(out + "/campaign_daily", 0).string() + ".tmp", false);
  for (auto &[key, a] : ca) {
    auto [camp, day] = key;
    campaign_file.append(camps.names[camp], day, a.sessions, a.purchasing,
                         a.revenue, &channels[camp], a.assist);
  }
  campaign_file.close();
  finish_parts(out + "/user_daily", threads);
  finish_parts(out + "/campaign_daily", 1);
  cerr << "core seconds "
       << chrono::duration<double>(chrono::steady_clock::now() - begin).count()
       << "\n";
}

int main(int argc, char **argv) {
  auto begin = chrono::steady_clock::now();
  string data = getenv("DATA_DIR") ? getenv("DATA_DIR")
                                   : (argc > 1 ? argv[1] : "/data"),
         out = getenv("OUTPUT_DIR") ? getenv("OUTPUT_DIR")
                                    : (argc > 2 ? argv[2] : "/output");
  string manifest;
  {
    ifstream f(data + "/manifest.json");
    manifest.assign(istreambuf_iterator<char>(f), {});
  }
  auto val = [&](string k) {
    auto p = manifest.find('"' + k + '"');
    p = manifest.find(':', p);
    p = manifest.find('"', p);
    auto q = manifest.find('"', p + 1);
    return manifest.substr(p + 1, q - p - 1);
  };
  ll start = timestamp(val("window_start")), end = timestamp(val("window_end"));
  if (start == bad_time || end == bad_time || start >= end)
    throw runtime_error("missing or invalid manifest window");
  // Unsigned subtraction also covers a window spanning the epoch.
  const uint64_t span = uint64_t(end) - uint64_t(start);
  StrMap<int> products;
  int unknown = cats.id("unknown");
  rows<2>(data + "/products.csv",
      [&](auto &f) { products[string(f[0])] = cats.id(f[1]); });
  auto run = [&]<class Time>() {
    // The validated price cap of 10,000,000 cents fits in 24 bits.
    if (cats.names.size() <= 256)
      process<Time, PackedPurchase>(data, out, start, end, begin, products, unknown);
    else
      process<Time, WidePurchase>(data, out, start, end, begin, products, unknown);
  };
  if (span - 1 <= UINT32_MAX)
    run.operator()<uint32_t>();
  else
    run.operator()<ll>();
}
