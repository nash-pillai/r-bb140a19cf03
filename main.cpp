#include <bits/stdc++.h>
#include <filesystem>
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
  return era * 146097 + (yo * 365 + yo / 4 - yo / 100 + doy) - 719468;
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
void read_range(const fs::path &file, uint64_t begin, uint64_t end, int fields,
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
} users, cats, camps;
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
struct Event {
  ll ts = 0;
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
    if (s.front() == 'e' && s.size() <= 9) {
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
struct Kept {
  uint64_t key;
  ll ingest;
  Event e;
};
struct Click {
  ll ts;
  int user, camp;
  string id;
};
struct Agg {
  ll sessions = 0, events = 0, revenue = 0, purchasing = 0, assist = 0;
  map<int, ll> cat;
};
string quote(const string &s) { return "\"" + s + "\""; }
int main(int argc, char **argv) {
  auto begin = chrono::steady_clock::now();
  auto checkpoint = [&](const char *label) {
    cerr
        << label << " seconds "
        << chrono::duration<double>(chrono::steady_clock::now() - begin).count()
        << "\n";
  };
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
    Strings unlinked;
    vector<int> resolved;
  };
  auto ranges = line_ranges(data + "/events");
  vector<Reader> readers(threads);
  vector<int> canonical(parent.size());
  for (size_t i = 0; i < parent.size(); ++i)
    canonical[i] = root(i);
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
          ingest - ts <= DAY) {
        auto known = as_const(users.ids).find(f[1]);
        e.user = known == users.ids.end() ? -2 - reader.unlinked.id(f[1])
                                          : canonical[known->second];
        e.ts = ts;
        e.price = price;
        if (purchase) {
          auto product = as_const(products).find(f[5]);
          e.cat = product == products.end() ? unknown : product->second;
        }
      }
      // Invalid but well-formed copies still participate in deduplication.
      auto key = keys.get(f[0]);
      auto part = Hash{}(f[0]) % partitions;
      reader.records[part].push_back({key, ingest, e});
    });
  });
  for (auto &reader : readers) {
    reader.resolved.reserve(reader.unlinked.names.size());
    for (auto &name : reader.unlinked.names)
      reader.resolved.push_back(uid(name));
    reader.unlinked = {};
  }
  checkpoint("event parse");
  vector<Groups<Event>> surviving(threads);
  parallel_jobs(partitions, threads, [&](int worker, size_t part) {
    size_t count = 0;
    for (auto &reader : readers)
      count += reader.records[part].size();
    vector<Kept> records;
    records.reserve(count);
    for (auto &reader : readers) {
      auto &batch = reader.records[part];
      for (auto &record : batch) {
        int &u = record.e.user;
        if (u < -1)
          u = reader.resolved[-2 - u];
        records.push_back(record);
      }
      vector<Kept>().swap(batch);
    }
    sort(records.begin(), records.end(),
         [](auto &a, auto &b) { return a.key < b.key; });
    auto &groups = surviving[worker];
    for (size_t i = 0; i < count;) {
      size_t winner = i++;
      while (i < count && records[i].key == records[winner].key) {
        if (records[i].ingest < records[winner].ingest)
          winner = i;
        ++i;
      }
      const auto &event = records[winner].e;
      if (event.user >= 0)
        groups[event.user % person_groups].push_back(event);
    }
  });
  checkpoint("event dedup");
  canonical.resize(parent.size());
  for (size_t i = 0; i < parent.size(); ++i)
    canonical[i] = root(i);
  vector<Groups<Click>> click_batches(threads);
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
      auto person = as_const(users.ids).find(f[1]);
      if (person == users.ids.end())
        return;
      int user = canonical[person->second];
      click_batches[worker][user % person_groups].push_back(
          {ts, user, c->second, string(f[0])});
    });
  });
  checkpoint("click parse");
  vector<map<pair<int, ll>, Agg>> totals(threads);
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
         [](auto &a, auto &b) { return tie(a.user, a.ts) < tie(b.user, b.ts); });
    sort(clicks.begin(), clicks.end(), [](auto &a, auto &b) {
      if (a.user != b.user)
        return a.user < b.user;
      if (a.ts != b.ts)
        return a.ts < b.ts;
      return a.id > b.id;
    });
    // Identical copies are adjacent. Descending ID leaves the smallest tie last.
    clicks.erase(unique(clicks.begin(), clicks.end(), [](auto &a, auto &b) {
      return a.id == b.id;
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
        ll sessionStart = events[ei].ts, day = sessionStart / DAY - (sessionStart % DAY < 0);
        while (right < ce && clicks[right].ts <= sessionStart)
          active[clicks[right++].camp]++;
        while (left < right && clicks[left].ts < sessionStart - WEEK) {
          int camp = clicks[left++].camp;
          if (--active[camp] == 0)
            active.erase(camp);
        }
        int credit = right > left ? clicks[right - 1].camp : organic;
        auto &us = ua[day];
        auto &cs = ca[{credit, day}];
        us.sessions++;
        cs.sessions++;
        bool purchase = false;
        ll prev = events[ei].ts;
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
        } while (ei < ee && events[ei].ts - prev <= 1800000);
        cs.purchasing += purchase;
        for (auto &[camp, n] : active)
          ca[{camp, day}].assist++;
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
  map<pair<int, ll>, Agg> ca;
  for (const auto &local : totals) {
    for (const auto &[key, a] : local) {
      auto &total = ca[key];
      total.sessions += a.sessions;
      total.purchasing += a.purchasing;
      total.revenue += a.revenue;
      total.assist += a.assist;
    }
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
