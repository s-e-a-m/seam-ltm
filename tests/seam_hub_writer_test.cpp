// The hub's single writer of the session sheet (Task 5).
//
// The hub is one static per process, so every case here shares it. Each case
// starts with no member registered and no writer alive, points SEAM_HUB_CONF
// at a fresh conf whose folder is a fresh temp dir, and sets a short grace
// (the 10 s default would make every case wait). It ends, through a Cleanup
// guard, by waiting for the writer to exit and removing its temp dirs. The
// next register then starts a new session that re-reads the conf
// (seam_hub.cpp, Sheet::begin). That is what makes the cases independent.
//
// Timing. The machine may be loaded (ctest runs this after long DSP tests),
// so a property that is "eventually" is polled for, with a generous timeout
// that only a failure ever reaches; a fixed sleep stays only where the wait
// itself is the point (a grace that must be over). A wall-clock upper bound
// stays only where the bound IS the property (the 2 s kLastLeaveWait of the
// last unregister and of quit), with kSlackMs for the scheduler on top, and a
// lower bound only where it proves the wait really happened.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Test hooks exported by the dylib, not part of seam_hub.h.
extern "C" void    seam_hub_test_set_grace_ms(int32_t ms);
extern "C" void    seam_hub_test_stall_writer(int32_t ms);
extern "C" int32_t seam_hub_test_writer_alive(void);
extern "C" int32_t seam_hub_test_teardown(void);
extern "C" void    seam_hub_test_teardown_gap_ms(int32_t ms);

namespace {

constexpr int32_t kTestGraceMs = 200;
constexpr double  kLastLeaveWaitMs = 2000.0;   // seam_hub.cpp, kLastLeaveWait
constexpr double  kSlackMs = 1000.0;           // scheduler slack on a bound that is the property
constexpr int     kEventuallyMs = 20000;       // a poll's timeout: only a failure reaches it

std::vector<fs::path>& tempDirs() {
    static std::vector<fs::path> v;
    return v;
}

// Polls `cond` every 10 ms until it holds or `ms` have passed (measured on
// the clock, not by counting sleeps, which overrun under load).
template <class F>
bool eventually(F cond, int ms = kEventuallyMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        if (cond()) return true;
        if (std::chrono::steady_clock::now() >= until) return cond();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool waitWriterGone(int ms = kEventuallyMs) {
    return eventually([] { return !seam_hub_test_writer_alive(); }, ms);
}

// At the end of each case: the writer exits (the sheet is closed), then the
// temp dirs this case made are removed.
struct Cleanup {
    ~Cleanup() {
        seam_hub_test_stall_writer(0);
        CHECK(waitWriterGone());
        std::error_code ec;
        for (const auto& d : tempDirs()) fs::remove_all(d, ec);
        tempDirs().clear();
    }
};

const uint32_t kTable = SEAM_HUB_BIT(kSeamHubBandTable);

fs::path freshDir(const char* tag) {
    std::string tmpl = (fs::temp_directory_path() / (std::string("seam-hub-") + tag + "-XXXXXX")).string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    REQUIRE(mkdtemp(buf.data()) != nullptr);
    tempDirs().push_back(buf.data());
    return fs::path(buf.data());
}

// A fresh conf naming `folder`, set as SEAM_HUB_CONF. Only between sessions:
// the writer reads the env and the conf at session start.
std::string verbOf(const std::string& l);

// Returns the conf's folder, where the hub spills held lines (sheets-held/).
fs::path useFolder(const fs::path& folder, bool spaced = false) {
    REQUIRE(waitWriterGone());
    const fs::path confDir = freshDir("conf");
    const fs::path conf = confDir / "hub.conf";
    std::ofstream(conf) << "# test conf\n"
                        << (spaced ? "  sheet-folder =  " : "sheet-folder=") << folder.string()
                        << (spaced ? "  \n" : "\n");
    setenv("SEAM_HUB_CONF", conf.c_str(), 1);
    seam_hub_test_set_grace_ms(kTestGraceMs);
    return confDir;
}

// A folder that cannot be created until `blocker` is removed: its parent is
// a regular file. Removing the file is the folder coming back.
struct Blocked {
    fs::path blocker, folder;
    Blocked() {
        blocker = freshDir("blocked") / "volume";
        std::ofstream(blocker) << "not a directory\n";
        folder = blocker / "sheets";
    }
    void release() { fs::remove(blocker); }
};

SeamHubSheetStatus status() {
    SeamHubSheetStatus st;
    std::memset(&st, 0, sizeof st);
    seam_hub_v1_sheet_status(seam_hub_v1_get(), &st);
    return st;
}

std::string fileText(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int countVerb(const std::vector<std::string>& lines, const char* verb) {
    int n = 0;
    for (const auto& l : lines) n += verbOf(l) == verb;
    return n;
}

std::vector<fs::path> sheets(const fs::path& folder) {
    std::vector<fs::path> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 15 && n.compare(n.size() - 15, 15, "-seam-sheet.txt") == 0) v.push_back(e.path());
    }
    return v;
}

std::vector<std::string> fileLines(const fs::path& p) {
    std::ifstream f(p);
    std::vector<std::string> v;
    for (std::string l; std::getline(f, l);) v.push_back(l);
    return v;
}

// The verb of an event line, "" for a header or a continuation.
std::string verbOf(const std::string& l) {
    static const std::regex ev(R"(^\d\d:\d\d:\d\d\.\d\d\d  \S+ *  (\S+).*$)");
    std::smatch m;
    return std::regex_match(l, m, ev) ? m[1].str() : std::string();
}

SeamHubProposal table(uint32_t stone) {
    SeamHubProposal p;
    std::memset(&p, 0, sizeof p);
    p.type = kSeamHubBandTable;
    p.stoneId = stone;
    p.u.table.bandCount = SEAM_HUB_BANDS;
    p.u.table.integrationSec = 2.0;
    for (int i = 0; i < SEAM_HUB_BANDS; ++i) p.u.table.bands[i].hz = 20.0 * (i + 1);
    return p;
}

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// The recent lines (seam_hub_v1_recent_lines), as one string.
std::string recentText() {
    static char buf[64 * 1024];
    buf[0] = '\0';
    seam_hub_v1_recent_lines(seam_hub_v1_get(), buf, sizeof buf);
    return buf;
}

// Whether the recent lines carry `needle`. The recent lines are the
// process's, shared by every case: a case looks for a line only it writes.
bool recentHas(const std::string& needle) {
    return recentText().find(needle) != std::string::npos;
}

// The writer has started this session's sheet in `folder`: it has picked the
// name (and published it), which it does before its first attempt to open
// it. The folder is part of the wait: until then the status still shows the
// previous case's sheet.
bool sheetNamedIn(const std::string& folder) {
    return eventually([&] { return std::string(status().path).rfind(folder + "/", 0) == 0; });
}

} // namespace

TEST_CASE("a session writes one file with arrivals and departures") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("one");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "strx", kTable, 0, 0);
    const int32_t b = seam_hub_v1_register(hub, "bastone", 0, kTable, 0);
    SeamHubProposal p = table(1);
    REQUIRE(seam_hub_v1_post(hub, a, &p) != 0);
    seam_hub_v1_unregister(hub, a);
    seam_hub_v1_unregister(hub, b);
    REQUIRE(waitWriterGone());   // the sheet closed (the flush on return has its own case)

    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    std::vector<std::string> verbs;
    size_t rows = 0;
    for (const auto& l : fileLines(files[0])) {
        const std::string v = verbOf(l);
        if (!v.empty()) verbs.push_back(v);
        if (l.rfind("  | ", 0) == 0) ++rows;
    }
    CHECK(verbs == std::vector<std::string>{"arrives", "arrives", "says", "leaves", "leaves"});
    CHECK(rows == 1 + SEAM_HUB_BANDS);   // the table in full: title plus one row per band
    const auto all = fileLines(files[0]);
    REQUIRE(all.size() >= 2);
    CHECK(all[0] == "# SEAM hub sheet");
    CHECK(all[1].rfind("# started: ", 0) == 0);
}

TEST_CASE("lines from four threads are never interleaved") {
    Cleanup cleanup;   // the sheet dir from the env is not a temp dir: it stays for the roundtrip
    SeamHub* hub = seam_hub_v1_get();
    // The ctest hub_sheet_roundtrip runs tools/hub-sheet.py --check-dir on this
    // folder after this binary, so the folder must hold this sheet alone.
    fs::path folder;
    if (const char* d = std::getenv("SEAM_HUB_TEST_SHEET_DIR")) {
        folder = d;
        std::error_code ec;
        fs::remove_all(folder, ec);
        fs::create_directories(folder);
    } else {
        folder = freshDir("four");
    }
    useFolder(folder);

    std::vector<int32_t> handles;
    const char* names[4] = {"strx", "bastone", "bastooge", "detector"};
    for (const char* n : names) handles.push_back(seam_hub_v1_register(hub, n, kTable, 0, 0));
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([hub, h = handles[t], t] {
            for (int i = 0; i < 250; ++i) {
                SeamHubProposal p = table((uint32_t)t + 1);
                seam_hub_v1_post(hub, h, &p);
            }
        });
    for (auto& t : ts) t.join();
    for (int32_t h : handles) seam_hub_v1_unregister(hub, h);
    REQUIRE(waitWriterGone());

    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    int says = 0, arrives = 0, leaves = 0, rowsAfterSays = 0;
    bool inSays = false, ok = true;
    for (const auto& l : fileLines(files[0])) {
        const std::string v = verbOf(l);
        if (!v.empty()) {
            if (inSays && rowsAfterSays != 1 + SEAM_HUB_BANDS) ok = false;
            inSays = (v == "says");
            rowsAfterSays = 0;
            says += v == "says"; arrives += v == "arrives"; leaves += v == "leaves";
        } else if (l.rfind("  | ", 0) == 0) {
            ++rowsAfterSays;
        } else if (l.rfind("# ", 0) != 0) {
            ok = false;   // neither event, row nor header: a broken line
        }
    }
    CHECK(ok);
    CHECK(says == 1000);
    CHECK(arrives == 4);
    CHECK(leaves == 4);
}

TEST_CASE("the last unregister flushes and returns") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("flush");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "flusher", kTable, 0, 0);
    SeamHubProposal p = table(3);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    REQUIRE(id != 0);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    // Returned on the writer's acknowledgement, before the bound would have
    // released it (a few ms on an idle machine); the file is read at once,
    // with no wait for the writer: that is the property.
    CHECK(msSince(t0) < kLastLeaveWaitMs);
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    bool found = false;
    for (const auto& l : fileLines(files[0]))
        if (l.find("says      band-table #" + std::to_string(id) + "  stone 3") != std::string::npos) found = true;
    CHECK(found);
}

TEST_CASE("an unreachable folder holds lines and never blocks") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path confDir = useFolder("/Volumes/seam-hub-no-such-volume/sheets");
    const int32_t a = seam_hub_v1_register(hub, "held", kTable, 0, 0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        SeamHubProposal p = table(1);
        seam_hub_v1_post(hub, a, &p);
    }
    // No disk on the poster's path: far below the bound a blocking post
    // would show (the writer keeps failing on the folder meanwhile).
    CHECK(msSince(t0) < kSlackMs);
    REQUIRE(sheetNamedIn("/Volumes/seam-hub-no-such-volume/sheets"));   // started; tried each tick
    SeamHubSheetStatus st;
    std::memset(&st, 0xff, sizeof st);
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 0u);
    CHECK(st.pendingLines == 101u);   // 100 says + 1 arrival
    const std::string oldPath = st.path;
    REQUIRE(oldPath.rfind("/Volumes/seam-hub-no-such-volume/sheets/", 0) == 0);

    // The operator moves on: a new folder and a new sheet. The held lines
    // belong to the old sheet, so they do not enter the new one: they are
    // spilled beside the conf, and the new sheet says where they went.
    const fs::path folder = freshDir("back");
    CHECK(seam_hub_v1_sheet_set_folder(hub, folder.c_str()) == 0);
    seam_hub_v1_sheet_new(hub);
    CHECK(eventually([&] { seam_hub_v1_sheet_status(hub, &st); return st.writing == 1u; }));
    CHECK(std::string(st.path).rfind(folder.string(), 0) == 0);
    seam_hub_v1_unregister(hub, a);
    // The last unregister returns once its `leaves` is written or held; a
    // young one is held for the fold window, and written by the sheet's close.
    REQUIRE(waitWriterGone());

    const fs::path spill = confDir / "sheets-held" / fs::path(oldPath).filename();
    REQUIRE(fs::exists(spill));
    const auto held = fileLines(spill);
    REQUIRE(held.size() >= 3);
    CHECK(held[0] == "# SEAM hub sheet");
    CHECK(held[2].find("hub           sheet     sheet renewed, folder unreachable (" + oldPath +
                       "), 101 lines held here") != std::string::npos);
    CHECK(countVerb(held, "arrives") == 1);
    CHECK(countVerb(held, "says") == 100);

    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    bool notice = false;
    for (const auto& l : lines)
        if (l.find("hub           sheet     previous sheet " + oldPath + " unreachable, 101 lines moved to " +
                   spill.string()) != std::string::npos) notice = true;
    CHECK(notice);
    CHECK(countVerb(lines, "says") == 0);
    CHECK(countVerb(lines, "leaves") == 1);
}

TEST_CASE("held lines land in their own sheet when the folder returns, even after the grace") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    Blocked blocked;
    useFolder(blocked.folder);
    const int32_t a = seam_hub_v1_register(hub, "held", kTable, 0, 0);
    for (int i = 0; i < 3; ++i) { SeamHubProposal p = table(1); seam_hub_v1_post(hub, a, &p); }
    REQUIRE(sheetNamedIn(blocked.folder.string()));
    const std::string path = status().path;
    REQUIRE(path.rfind(blocked.folder.string(), 0) == 0);
    CHECK(status().writing == 0u);
    seam_hub_v1_unregister(hub, a);
    // The grace (200 ms) is long over: the writer stays, holding the lines.
    // A real wait, past the grace; a slow machine only makes it longer.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    CHECK(seam_hub_test_writer_alive() == 1);
    CHECK(status().pendingLines == 5u);   // arrives, 3 says, leaves
    CHECK(std::string(status().path) == path);          // the same sheet, not a new one

    blocked.release();
    REQUIRE(waitWriterGone());
    const auto files = sheets(blocked.folder);
    REQUIRE(files.size() == 1);
    CHECK(files[0].string() == path);
    const auto lines = fileLines(files[0]);
    REQUIRE(lines.size() >= 3);
    CHECK(lines[0] == "# SEAM hub sheet");
    CHECK(lines[2].find("hub           sheet     folder unreachable, 5 lines held") != std::string::npos);
    CHECK(countVerb(lines, "arrives") == 1);
    CHECK(countVerb(lines, "says") == 3);
    CHECK(countVerb(lines, "leaves") == 1);
    CHECK(verbOf(lines.back()) == "leaves");
    CHECK(status().pendingLines == 0u);
}

TEST_CASE("held lines are spilled to the local fallback at quit") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    Blocked blocked;
    const fs::path confDir = useFolder(blocked.folder);
    const int32_t a = seam_hub_v1_register(hub, "held", kTable, 0, 0);
    for (int i = 0; i < 3; ++i) { SeamHubProposal p = table(1); seam_hub_v1_post(hub, a, &p); }
    seam_hub_v1_unregister(hub, a);
    REQUIRE(sheetNamedIn(blocked.folder.string()));
    const std::string path = status().path;
    REQUIRE(!path.empty());
    REQUIRE(seam_hub_test_writer_alive() == 1);   // holding

    const auto t0 = std::chrono::steady_clock::now();
    CHECK(seam_hub_test_teardown() == 1);
    CHECK(msSince(t0) < kLastLeaveWaitMs + kSlackMs);
    CHECK(status().pendingLines == 0u);
    const fs::path spill = confDir / "sheets-held" / fs::path(path).filename();
    REQUIRE(fs::exists(spill));
    const auto lines = fileLines(spill);
    REQUIRE(lines.size() >= 3);
    CHECK(lines[0] == "# SEAM hub sheet");
    CHECK(lines[2].find("hub           sheet     quit, folder unreachable (" + path + "), 5 lines held here") !=
          std::string::npos);
    CHECK(countVerb(lines, "arrives") == 1);
    CHECK(countVerb(lines, "says") == 3);
    CHECK(countVerb(lines, "leaves") == 1);
    // The released folder never received anything.
    blocked.release();
    CHECK(sheets(blocked.folder).empty());
}

TEST_CASE("a sheet with no free name is never appended to another; its lines are spilled") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("full");
    const fs::path confDir = useFolder(folder);
    // Every name this process could take in the next few seconds is taken.
    // Taking them is itself slow (999 files a second) and the machine may be
    // loaded, so the window is extended until it reaches at least 3 s past
    // the moment the register below runs.
    const std::time_t now = std::time(nullptr);
    for (std::time_t t = now; t < std::time(nullptr) + 4 && t < now + 30; ++t) {
        std::tm tm{};
        localtime_r(&t, &tm);
        char stem[96];
        std::snprintf(stem, sizeof stem, "%04d-%02d-%02d-%02d%02d%02d-%d", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
        for (int n = 1; n < 1000; ++n)
            std::ofstream(folder / (std::string(stem) + (n > 1 ? "-" + std::to_string(n) : "") + "-seam-sheet.txt"));
    }
    const size_t before = sheets(folder).size();
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "full", 0, 0, 0));
    REQUIRE(waitWriterGone());
    CHECK(status().path[0] == '\0');
    const auto files = sheets(folder);
    CHECK(files.size() == before);
    bool allEmpty = true;
    for (const auto& f : files) allEmpty = allEmpty && fs::file_size(f) == 0;
    CHECK(allEmpty);
    std::vector<fs::path> held;
    for (const auto& e : fs::directory_iterator(confDir / "sheets-held")) held.push_back(e.path());
    REQUIRE(held.size() == 1);
    const auto lines = fileLines(held[0]);
    CHECK(countVerb(lines, "arrives") == 1);
    CHECK(countVerb(lines, "leaves") == 1);
    CHECK(fileText(held[0]).find("session ended with nowhere to write (no path)") != std::string::npos);
}

TEST_CASE("the recent lines carry rows and state lines") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    useFolder(freshDir("recent"));
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink) | kTable, 0, 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 2;
    seam_hub_v1_publish_state(hub, a, &s);
    // A tick writes the state line; unchanged, no later tick writes another.
    REQUIRE(eventually([] { return recentHas("multipink#1   state     pink  stone 2"); }));
    SeamHubProposal p = table(2);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    REQUIRE(id != 0);
    // Rows reach the recent lines after the fold, on the writer's tick. The
    // table's own `says` line is what is waited for (an earlier case's table
    // has the same rows), and its rows come with it, in one entry.
    REQUIRE(eventually([id] { return recentHas("says      band-table #" + std::to_string(id) + "  stone 2"); }));
    static char buf[64 * 1024];
    const int32_t n = seam_hub_v1_recent_lines(hub, buf, sizeof buf);
    const std::string all(buf);
    CHECK(n > 3);
    CHECK(all.find("multipink#1   state     pink  stone 2") != std::string::npos);
    CHECK(all.find("  | band  hz        comp_db  level_db  settled\n") != std::string::npos);
    CHECK(all.substr(all.size() - 1) == "\n");
    // A short cap keeps the newest: the table's last row.
    char small[64];
    CHECK(seam_hub_v1_recent_lines(hub, small, sizeof small) >= 1);
    CHECK(std::string(small).find("  | 20k") == 0);
    CHECK(seam_hub_v1_recent_lines(nullptr, small, sizeof small) == 0);
    CHECK(seam_hub_v1_recent_lines(hub, small, 0) == 0);
    seam_hub_v1_unregister(hub, a);
}

TEST_CASE("the file name carries the pid") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("pid");
    useFolder(folder, /*spaced=*/true);   // also: spaces around key, '=' and value
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "pid", 0, 0, 0));
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const std::string name = files[0].filename().string();
    CHECK(name.find("-" + std::to_string(getpid()) + "-") != std::string::npos);
    CHECK(std::regex_match(name, std::regex(R"(\d{4}-\d\d-\d\d-\d{6}-\d+(-\d+)?-seam-sheet\.txt)")));
}

TEST_CASE("a state change is written once, not every tick") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("state");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0, 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 1;
    s.active = 1;
    s.levelDb = -23.0;
    s.sampleRate = 48000.0;
    s.u.pink.slotStart = 0;
    s.u.pink.slotCount = 4;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; msSince(t0) < 1000.0; ++i) {
        // Bytes of the union's inactive glide member change on every publish:
        // the comparison is field-wise by kind, so they are not a change.
        s.u.glide.f1 = i;
        seam_hub_v1_publish_state(hub, a, &s);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // At least one tick saw the member (four in the second, on an idle machine).
    REQUIRE(eventually([] {
        return recentHas("multipink#1   state     pink  stone 1  slots 0-3  -23.0 dB  active  48000 Hz");
    }));
    seam_hub_v1_unregister(hub, a);
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    int states = 0;
    for (const auto& l : fileLines(files[0]))
        if (verbOf(l) == "state") {
            ++states;
            CHECK(l.find("multipink#1   state     pink  stone 1  slots 0-3  -23.0 dB  active  48000 Hz") != std::string::npos);
        }
    CHECK(states == 1);
}

TEST_CASE("a NaN level is written once, not every tick") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("nan");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0, 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 41;   // a line no other case writes
    s.levelDb = std::nan("");
    seam_hub_v1_publish_state(hub, a, &s);
    REQUIRE(eventually([] { return recentHas("multipink#1   state     pink  stone 41"); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));   // three ticks or more after the first line
    seam_hub_v1_unregister(hub, a);
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    CHECK(countVerb(fileLines(files[0]), "state") == 1);
}

TEST_CASE("sheet_status and sheet_set_folder refuse nulls") {
    Cleanup cleanup;
    useFolder(freshDir("nulls"));
    CHECK(seam_hub_v1_sheet_set_folder(seam_hub_v1_get(), "relative/sheets") != 0);
    SeamHubSheetStatus st;
    seam_hub_v1_sheet_status(nullptr, &st);
    seam_hub_v1_sheet_status(seam_hub_v1_get(), nullptr);
    CHECK(seam_hub_v1_sheet_set_folder(seam_hub_v1_get(), nullptr) != 0);
    CHECK(seam_hub_v1_sheet_set_folder(seam_hub_v1_get(), "") != 0);
    seam_hub_v1_sheet_new(nullptr);
}

TEST_CASE("a state change produces a second line") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("state2");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0, 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 42;   // lines no other case writes
    s.levelDb = -23.0;
    seam_hub_v1_publish_state(hub, a, &s);
    // A tick must see each state before the next replaces it (the last
    // unregister writes no state): wait for each line rather than a time.
    REQUIRE(eventually([] { return recentHas("multipink#1   state     pink  stone 42  slots 0+0  -23.0 dB"); }));
    s.levelDb = -20.0;
    seam_hub_v1_publish_state(hub, a, &s);
    REQUIRE(eventually([] { return recentHas("multipink#1   state     pink  stone 42  slots 0+0  -20.0 dB"); }));
    seam_hub_v1_unregister(hub, a);
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    CHECK(countVerb(lines, "state") == 2);
}

TEST_CASE("a toggle within the grace continues the same sheet") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("toggle");
    useFolder(folder);
    // The real grace, so the toggle surely falls within it however slow the
    // machine; the short one again before the unregister that ends the case.
    seam_hub_test_set_grace_ms(10000);
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(seam_hub_test_writer_alive() == 1);   // lingering
    SeamHubSheetStatus st;
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 1u);                     // the sheet stays open while it lingers
    const int32_t back = seam_hub_v1_register(hub, "strx", 0, 0, 0);
    seam_hub_test_set_grace_ms(kTestGraceMs);
    seam_hub_v1_unregister(hub, back);
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    int headers = 0;
    for (const auto& l : lines) headers += l == "# SEAM hub sheet";
    CHECK(headers == 1);
    // The toggle (a leave and an arrival 100 ms apart, the same declaration)
    // is one `resumes` line: the host only bypassed the member.
    CHECK(countVerb(lines, "arrives") == 1);
    CHECK(countVerb(lines, "resumes") == 1);
    CHECK(countVerb(lines, "leaves") == 1);
}

TEST_CASE("after the grace the sheet is closed and the writer is gone") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("grace");
    useFolder(folder);
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0, 0));
    REQUIRE(waitWriterGone());
    SeamHubSheetStatus st;
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 0u);
    CHECK(sheets(folder).size() == 1);
    // A later register opens a new sheet (a new file, even within the same
    // second: the name takes a -2 suffix).
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0, 0));
    REQUIRE(waitWriterGone());
    CHECK(sheets(folder).size() == 2);
}

TEST_CASE("the last unregister returns within its 2 s bound even if the writer is stuck") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("stuck");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "stuck", 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the first pass is done
    seam_hub_test_stall_writer(5000);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    const double ms = msSince(t0);
    CHECK(ms < kLastLeaveWaitMs + kSlackMs);
    CHECK(ms > 1500.0);   // it did wait for the leaves line, up to the bound
    // A register racing the stuck writer does not wait on the disk either:
    // the stall has about 3 s left, so a register that waited would show it.
    const auto t1 = std::chrono::steady_clock::now();
    const int32_t b = seam_hub_v1_register(hub, "stuck", 0, 0, 0);
    CHECK(msSince(t1) < kSlackMs);
    seam_hub_test_stall_writer(0);
    seam_hub_v1_unregister(hub, b);
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    CHECK(countVerb(lines, "leaves") == 2);   // nothing lost, only late
}

TEST_CASE("quit with a lingering idle writer returns at once and the sheet ends with leaves") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("quit");
    useFolder(folder);
    seam_hub_test_set_grace_ms(10000);   // the real grace: the writer is lingering at quit
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0, 0));
    REQUIRE(seam_hub_test_writer_alive() == 1);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(seam_hub_test_teardown() == 1);
    // At once: well inside both the 2 s bound and the 10 s grace.
    CHECK(msSince(t0) < kSlackMs);
    CHECK(seam_hub_test_writer_alive() == 0);
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    REQUIRE(!lines.empty());
    CHECK(verbOf(lines.back()) == "leaves");
}

TEST_CASE("quit with a stuck writer returns within its 2 s bound") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("quitstuck");
    useFolder(folder);
    seam_hub_test_set_grace_ms(10000);
    // Members gone first, so nothing is left held after the detached writer
    // finishes (a held line would land in the next case's sheet).
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0, 0));
    seam_hub_test_stall_writer(5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // a lingering tick is now stuck
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(seam_hub_test_teardown() == 0);   // detached, not joined
    const double ms = msSince(t0);
    CHECK(ms < kLastLeaveWaitMs + kSlackMs);
    CHECK(ms > 1500.0);   // it did wait for the writer, up to the bound
    // Released, the detached writer finishes its pass under stop, closes the
    // sheet and exits; the hub it touches was never destroyed.
    seam_hub_test_stall_writer(0);
    REQUIRE(waitWriterGone());
    // And the hub works again: a new session starts a new writer.
    const int32_t a = seam_hub_v1_register(hub, "strx", 0, 0, 0);
    CHECK(seam_hub_test_writer_alive() == 1);
    seam_hub_test_set_grace_ms(kTestGraceMs);   // before the unregister that starts the grace
    seam_hub_v1_unregister(hub, a);
}

TEST_CASE("quit never joins a writer a racing register just started") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("quitrace");
    useFolder(folder);   // no writer alive now: teardown's bounded wait ends at once
    // Teardown sees no writer, then sleeps before it takes lifeMutex; a
    // register in that gap starts a new writer, which stays alive while the
    // member is registered. Teardown must not join it.
    // The register must land in the gap: the gap is wide, and the register
    // waits for the quit thread to be running (not for a guessed start-up
    // time), then 200 ms for teardown's first, lock-only step.
    seam_hub_test_teardown_gap_ms(1500);
    std::promise<void> started;
    auto quit = std::async(std::launch::async, [&started] {
        started.set_value();
        return seam_hub_test_teardown();
    });
    started.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const int32_t a = seam_hub_v1_register(hub, "strx", 0, 0, 0);
    REQUIRE(seam_hub_test_writer_alive() == 1);
    // A joined writer would never return while its member is registered.
    const bool returned = quit.wait_for(std::chrono::milliseconds(kEventuallyMs)) == std::future_status::ready;
    CHECK(returned);
    seam_hub_test_teardown_gap_ms(0);
    seam_hub_v1_unregister(hub, a);      // lets a wrongly joined writer exit, so a RED run ends
    const int32_t r = quit.get();
    CHECK(r == 0);                       // the live writer was detached, not joined
}

TEST_CASE("a host toggle is written as one resumes line") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("hosttoggle");
    useFolder(folder);
    // The real grace, so the close (which writes with no window) cannot come
    // between the leave and the return however slow the machine is.
    seam_hub_test_set_grace_ms(10000);
    // A name no other case uses: the recent lines are the process's.
    const int32_t a = seam_hub_v1_register(hub, "hosttoggle", 0, SEAM_HUB_BIT(kSeamHubPink), 0);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    const int32_t b = seam_hub_v1_register(hub, "hosttoggle", 0, SEAM_HUB_BIT(kSeamHubPink), 1);
    REQUIRE(msSince(t0) < 1000.0);   // the toggle fell within the fold window
    // One tick past the window, the pair is folded and written.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 + 250));
    REQUIRE(sheetNamedIn(folder.string()));
    const fs::path file = status().path;
    REQUIRE(eventually([&] { return countVerb(fileLines(file), "resumes") == 1; }));
    const auto lines = fileLines(file);
    CHECK(countVerb(lines, "arrives") == 1);
    CHECK(countVerb(lines, "resumes") == 1);
    CHECK(countVerb(lines, "leaves") == 0);
    bool after = false;
    for (const auto& l : lines)
        if (verbOf(l) == "resumes")
            after = std::regex_search(l, std::regex(R"(hosttoggle#1  resumes   after \d+ ms$)"));
    CHECK(after);
    // The detector shows what the sheet carries: the resumes, never the leave.
    const std::string recent = recentText();
    CHECK(recent.find("hosttoggle#1  resumes") != std::string::npos);
    CHECK(recent.find("hosttoggle#1  leaves") == std::string::npos);
    seam_hub_test_set_grace_ms(kTestGraceMs);   // before the unregister that ends the case
    seam_hub_v1_unregister(hub, b);
}

TEST_CASE("the last unregister does not wait for the fold window") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    useFolder(freshDir("nowait"));
    const int32_t a = seam_hub_v1_register(hub, "nowait", 0, 0, 0);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    // Acknowledged as held, not after the 1 s window.
    CHECK(msSince(t0) < 500.0);
}

TEST_CASE("a leave with nobody back is written after the window") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("lateleave");
    useFolder(folder);
    seam_hub_test_set_grace_ms(10000);   // the writer lingers: no close writes it
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "lateleave", 0, 0, 0));
    REQUIRE(sheetNamedIn(folder.string()));
    const fs::path file = status().path;
    // The window and two ticks; the slack only for a loaded machine.
    CHECK(eventually([&] {
        const auto l = fileLines(file);
        return !l.empty() && verbOf(l.back()) == "leaves";
    }, 1000 + 2 * 250 + (int)kSlackMs));
    CHECK(seam_hub_test_writer_alive() == 1);   // written by a tick, not by the close
    CHECK(seam_hub_test_teardown() == 1);       // ends the 10 s linger at once
}

TEST_CASE("a short grace writes a young leave before closing") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("shortgrace");
    useFolder(folder);   // grace 200 ms, shorter than the fold window
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "shortgrace", 0, 0, 0));
    REQUIRE(waitWriterGone());
    auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const fs::path first = files[0];
    const auto lines = fileLines(first);
    REQUIRE(!lines.empty());
    CHECK(verbOf(lines.back()) == "leaves");
    CHECK(lines.back().find("shortgrace#1") != std::string::npos);
    // The next session's sheet does not carry it.
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "second", 0, 0, 0));
    REQUIRE(waitWriterGone());
    files = sheets(folder);
    REQUIRE(files.size() == 2);
    for (const auto& f : files)
        if (f != first) CHECK(fileText(f).find("shortgrace#1") == std::string::npos);
}
