#include <bits/stdc++.h>
#include <fcntl.h>
#include <filesystem>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
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
bool digits(string_view s) {
  if (s.empty())
    return false;
  for (unsigned char c : s)
    if (c < '0' || c > '9')
      return false;
  return true;
}
ll number(string_view s, ll cap = LLONG_MAX) {
  ll n = 0;
  for (char c : s) {
    if (c < '0' || c > '9' || n > (cap - (c - '0')) / 10)
      return -1;
    n = n * 10 + (c - '0');
  }
  return s.empty() ? -1 : n;
}
ll days(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = y - era * 400;
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return ll(era) * 146097 + (yo * 365 + yo / 4 - yo / 100 + doy) - 719468;
}
constexpr ll bad_time = LLONG_MIN;
ll timestamp(string_view s) {
  if (s.size() < 5 || s[4] != '-') {
    ll epoch_ms = number(s);
    return epoch_ms < 0 ? bad_time : epoch_ms;
  }
  if ((s.size() != 20 && s.size() != 25) || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':')
    return bad_time;
  auto pair_at = [&](int i) {
    unsigned a = static_cast<unsigned char>(s[i]) - '0';
    unsigned b = static_cast<unsigned char>(s[i + 1]) - '0';
    return a <= 9 && b <= 9 ? int(a * 10 + b) : -1;
  };
  int century = pair_at(0), year = pair_at(2), m = pair_at(5), d = pair_at(8),
      h = pair_at(11), mi = pair_at(14), se = pair_at(17);
  if ((century | year | m | d | h | mi | se) < 0)
    return bad_time;
  int y = century * 100 + year;
  int md[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
    md[2] = 29;
  if (m < 1 || m > 12 || d < 1 || d > md[m] || h > 23 || mi > 59 || se > 59)
    return bad_time;
  ll off = 0;
  if (s.size() == 20) {
    if (s[19] != 'Z')
      return bad_time;
  } else {
    if ((s[19] != '+' && s[19] != '-') || s[22] != ':' ||
        !digits(s.substr(20, 2)) || !digits(s.substr(23, 2)))
      return bad_time;
    ll oh = number(s.substr(20, 2)), om = number(s.substr(23, 2));
    if (oh > 23 || om > 59)
      return bad_time;
    off = (oh * 60 + om) * 60000 * (s[19] == '+' ? 1 : -1);
  }
  return days(y, m, d) * DAY + (h * 3600 + mi * 60 + se) * 1000 - off;
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
template <class F>
void read_range(const fs::path &file, uint64_t begin, uint64_t end, int fields,
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
    read_stream_range(file, begin, end, fields, move(fun));
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
  values.reserve(fields);
  for (auto *line = previous + 1; line < range_end;) {
    const auto *next = newline(line);
    const auto *line_end = next ? next : file_end;
    if (csv(string_view(line, line_end - line), values, fields))
      fun(values);
    line = next ? next + 1 : file_end;
  }
}
template <class F> void rows(const fs::path &p, int fields, F fun) {
  for (const auto &file : csv_files(p))
    read_range(file, 0, fs::file_size(file), fields, fun);
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
vector<Range> line_ranges(const fs::path &dir) {
  constexpr uint64_t block = 8 << 20;
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

  static size_t probe(const vector<Slot> &table, uint64_t key) {
    uint64_t mixed = (key ^ (key >> 33)) * 0xff51afd7ed558ccdULL;
    const size_t mask = table.size() - 1;
    size_t index = (mixed ^ (mixed >> 33)) & mask;
    while (table[index].id >= 0 && table[index].key != key)
      index = (index + 1) & mask;
    return index;
  }

public:
  vector<string> names;

  void apply_roots(const vector<int> &roots) {
    for (auto &slot : slots)
      if (slot.id >= 0)
        slot.id = roots[slot.id];
    for (auto &entry : other)
      entry.second = roots[entry.second];
  }

  int find(string_view name) const {
    uint64_t key;
    if (packed_key(name, key))
      return slots[probe(slots, key)].id;
    const auto item = other.find(name);
    return item == other.end() ? -1 : item->second;
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
} users;

vector<int> parent;
int root(int x) {
  int r = x;
  while (parent[r] != r)
    r = parent[r];
  while (parent[x] != x) {
    int q = parent[x];
    parent[x] = r;
    x = q;
  }
  return r;
}
int uid(string_view s) {
  int x = users.id(s);
  while (parent.size() < users.names.size())
    parent.push_back(parent.size());
  return x;
}
template <class Time> struct EventRecord {
  Time ts = 0;
  int user = -1, cat = 0, price = 0;
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
template <class Time> struct KeptRecord {
  uint64_t key;
  ll ingest;
  EventRecord<Time> e;
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

string quote(const string &s) { return "\"" + s + "\""; }
template <class Time>
void process(const string &data, const string &out, ll start, ll end,
             chrono::steady_clock::time_point begin) {
  using Event = EventRecord<Time>;
  using Kept = KeptRecord<Time>;
  // Valid events have already passed the half-open window check on absolute time.
  const ll origin = is_same_v<Time, uint32_t> ? start : 0;
  auto checkpoint = [&](const char *label) {
    cerr
        << label << " seconds "
        << chrono::duration<double>(chrono::steady_clock::now() - begin).count()
        << "\n";
  };
  rows(data + "/links", 2, [&](auto &f) {
    if (f[0].empty() || f[1].empty())
      return;
    int a = root(uid(f[0])), b = root(uid(f[1]));
    if (a != b) {
      if (users.names[b] < users.names[a])
        swap(a, b);
      parent[b] = a;
    }
  });
  StrMap<int> products;
  int unknown = cats.id("unknown");
  rows(data + "/products.csv", 2,
       [&](auto &f) { products[string(f[0])] = cats.id(f[1]); });
  vector<string> channels;
  int organic = camps.id("organic");
  channels.push_back("organic");
  rows(data + "/campaigns.csv", 2, [&](auto &f) {
    int c = camps.id(f[0]);
    if (channels.size() <= size_t(c))
      channels.resize(c + 1);
    channels[c] = f[1];
  });
  checkpoint("links and dimensions");
  const int threads = clamp(getenv("NCPU") ? atoi(getenv("NCPU")) : 8, 1, 8);
  constexpr size_t partitions = 256;
  struct Reader {
    array<vector<Kept>, partitions> records;
    UserIndex unlinked;
    vector<int> resolved;
  };
  auto ranges = line_ranges(data + "/events");
  vector<Reader> readers(threads);
  // Publish the canonical IDs in the lookup table once links are complete.
  // Any later insertion is an unlinked singleton with its own name index.
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = root(i);
  users.apply_roots(parent);
  vector<int>().swap(parent);
  EventKeys keys;
  parallel_jobs(ranges.size(), threads, [&](int worker, size_t job) {
    auto &reader = readers[worker];
    const auto &range = ranges[job];
    read_range(range.file, range.begin, range.end, 7, [&](auto &f) {
      if (f[0].empty())
        return;
      ll ingest = timestamp(f[3]);
      ll ts = timestamp(f[2]);
      bool purchase = f[4] == "purchase";
      ll price = purchase ? number(f[6], 10000000) : 0;
      Event e;
      if (!f[1].empty() && ts >= start && ts < end &&
          (purchase || f[4] == "view" || f[4] == "cart") &&
          !f[5].empty() && (!purchase || price >= 1) &&
          (ingest <= ts || uint64_t(ingest) - uint64_t(ts) <= uint64_t(DAY))) {
        const int known = users.find(f[1]);
        e.user = known < 0 ? -2 - reader.unlinked.id(f[1]) : known;
        e.ts = static_cast<Time>(ts - origin);
        e.price = price;
        if (purchase) {
          auto product = as_const(products).find(f[5]);
          e.cat = product == products.end() ? unknown : product->second;
        }
      }
      // Invalid but well-formed copies still participate in deduplication.
      auto key = keys.get(f[0]);
      // SplitMix64 finalizer spreads both packed and interned IDs across readers'
      // buckets. Keep the exact key for equality inside each bucket.
      uint64_t h = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
      h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
      auto part = (h ^ (h >> 31)) % partitions;
      reader.records[part].push_back({key, ingest, e});
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
        } else if (record.ingest < winner->ingest) {
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
    read_range(range.file, range.begin, range.end, 4, [&](auto &f) {
      if (f[0].empty() || f[1].empty())
        return;
      ll ts = timestamp(f[3]);
      auto c = as_const(camps.ids).find(f[2]);
      if (ts < start - WEEK || ts >= end || c == camps.ids.end() ||
          c->second == organic)
        return;
      // A previously unseen, unlinked user has no events to credit or assist.
      const int person = users.find(f[1]);
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
  ofstream u(out + "/user_daily/raw.csv"), c(out + "/campaign_daily/raw.csv");
  u << "user_id,day,sessions,events,revenue_cents,top_category\n";
  c << "campaign_id,channel,day,sessions,purchasing_sessions,revenue_cents,"
       "assisted_sessions\n";
  mutex output_lock;
  atomic<size_t> valid_events{0};
  parallel_jobs(person_groups, threads, [&](int worker, size_t group) {
    auto events = gather(surviving, group);
    auto clicks = gather(click_batches, group);
    valid_events.fetch_add(events.size(), memory_order_relaxed);
    sort(events.begin(), events.end(),
         [](const Event &a, const Event &b) {
           if constexpr (is_same_v<Time, uint32_t>) {
             auto key = [](const Event &e) { return (uint64_t(e.user) << 32) | e.ts; };
             return key(a) < key(b);
           } else {
             return tie(a.user, a.ts) < tie(b.user, b.ts);
           }
         });
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
    ostringstream user_rows;
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
          us.revenue += e.price;
          cs.revenue += e.price;
          if (e.price) {
            purchase = true;
            us.cat[e.cat] += e.price;
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
        user_rows << quote(users.names[user]) << ',' << day << ',' << a.sessions << ','
          << a.events << ',' << a.revenue << ','
          << (top < 0 ? "" : quote(cats.names[top])) << '\n';
      }
      ci = ce;
    }
    lock_guard lock(output_lock);
    u << user_rows.view();
  });
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
  for (auto &[key, a] : ca) {
    auto [camp, day] = key;
    c << quote(camps.names[camp]) << ',' << quote(channels[camp]) << ',' << day
      << ',' << a.sessions << ',' << a.purchasing << ',' << a.revenue << ','
      << a.assist << '\n';
  }
  u.close();
  c.close();
  if (!u || !c)
    throw runtime_error("cannot write daily outputs");
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
  if (span - 1 <= UINT32_MAX)
    process<uint32_t>(data, out, start, end, begin);
  else
    process<ll>(data, out, start, end, begin);
}
