// The hub's single writer of the session sheet (Task 5).
//
// The hub is one static per process, so every case here shares it. Each case
// starts with no member registered and no writer alive, points SEAM_HUB_CONF
// at a fresh conf whose folder is a fresh temp dir, and sets a short grace
// (the 10 s default would make every case wait). It ends, through a Cleanup
// guard, by waiting for the writer to exit and removing its temp dirs. The
// next register then starts a new session that re-reads the conf
// (seam_hub.cpp, Sheet::begin). That is what makes the cases independent.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

namespace {

constexpr int32_t kTestGraceMs = 200;

std::vector<fs::path>& tempDirs() {
    static std::vector<fs::path> v;
    return v;
}

bool waitWriterGone(int ms = 5000) {
    for (int t = 0; t < ms; t += 10) {
        if (!seam_hub_test_writer_alive()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
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

void useFolder(const fs::path& folder, bool spaced = false) {
    REQUIRE(waitWriterGone());
    const fs::path conf = freshDir("conf") / "hub.conf";
    std::ofstream(conf) << "# test conf\n"
                        << (spaced ? "  sheet-folder =  " : "sheet-folder=") << folder.string()
                        << (spaced ? "  \n" : "\n");
    setenv("SEAM_HUB_CONF", conf.c_str(), 1);
    seam_hub_test_set_grace_ms(kTestGraceMs);
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

} // namespace

TEST_CASE("a session writes one file with arrivals and departures") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("one");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "strx", kTable, 0);
    const int32_t b = seam_hub_v1_register(hub, "bastone", 0, kTable);
    SeamHubProposal p = table(1);
    REQUIRE(seam_hub_v1_post(hub, a, &p) != 0);
    seam_hub_v1_unregister(hub, a);
    seam_hub_v1_unregister(hub, b);

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
    for (const char* n : names) handles.push_back(seam_hub_v1_register(hub, n, kTable, 0));
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
    const int32_t a = seam_hub_v1_register(hub, "flusher", kTable, 0);
    SeamHubProposal p = table(3);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    REQUIRE(id != 0);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    CHECK(msSince(t0) < 1000.0);
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
    useFolder("/Volumes/seam-hub-no-such-volume/sheets");
    const int32_t a = seam_hub_v1_register(hub, "held", kTable, 0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        SeamHubProposal p = table(1);
        seam_hub_v1_post(hub, a, &p);
    }
    CHECK(msSince(t0) < 50.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the writer has tried at least once
    SeamHubSheetStatus st;
    std::memset(&st, 0xff, sizeof st);
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 0u);
    CHECK(st.pendingLines == 101u);   // 100 says + 1 arrival

    const fs::path folder = freshDir("back");
    CHECK(seam_hub_v1_sheet_set_folder(hub, folder.c_str()) == 0);
    seam_hub_v1_sheet_new(hub);
    // Wait for the writer to open the new sheet, so the held count is exactly
    // what was held (the leaves line must not be counted with it).
    for (int i = 0; i < 200; ++i) {
        seam_hub_v1_sheet_status(hub, &st);
        if (st.writing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(st.writing == 1u);
    CHECK(std::string(st.path).rfind(folder.string(), 0) == 0);
    seam_hub_v1_unregister(hub, a);

    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    size_t mark = lines.size(), arrives = lines.size();
    int saysAfter = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("hub           sheet     folder unreachable, 101 lines held") != std::string::npos) mark = i;
        if (verbOf(lines[i]) == "arrives") arrives = i;
        if (verbOf(lines[i]) == "says" && i > mark) ++saysAfter;
    }
    REQUIRE(mark < lines.size());
    CHECK(arrives > mark);
    CHECK(saysAfter == 100);
}

TEST_CASE("the file name carries the pid") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("pid");
    useFolder(folder, /*spaced=*/true);   // also: spaces around key, '=' and value
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "pid", 0, 0));
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
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 1;
    s.active = 1;
    s.levelDb = -23.0;
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
    seam_hub_v1_unregister(hub, a);
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    int states = 0;
    for (const auto& l : fileLines(files[0]))
        if (verbOf(l) == "state") {
            ++states;
            CHECK(l.find("multipink#1   state     pink  stone 1  slots 0-3  -23.0 dB  active") != std::string::npos);
        }
    CHECK(states == 1);
}

TEST_CASE("a NaN level is written once, not every tick") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("nan");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.levelDb = std::nan("");
    seam_hub_v1_publish_state(hub, a, &s);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));   // three ticks or more
    seam_hub_v1_unregister(hub, a);
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
    const int32_t a = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), 0);
    SeamHubState s{};
    s.kind = kSeamHubPink;
    s.stoneId = 1;
    s.levelDb = -23.0;
    seam_hub_v1_publish_state(hub, a, &s);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    s.levelDb = -20.0;
    seam_hub_v1_publish_state(hub, a, &s);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    seam_hub_v1_unregister(hub, a);
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
    seam_hub_test_set_grace_ms(2000);
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(seam_hub_test_writer_alive() == 1);   // lingering
    SeamHubSheetStatus st;
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 1u);                     // the sheet stays open while it lingers
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    REQUIRE(waitWriterGone());
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    int headers = 0;
    for (const auto& l : lines) headers += l == "# SEAM hub sheet";
    CHECK(headers == 1);
    CHECK(countVerb(lines, "arrives") == 2);
    CHECK(countVerb(lines, "leaves") == 2);
}

TEST_CASE("after the grace the sheet is closed and the writer is gone") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("grace");
    useFolder(folder);
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    REQUIRE(waitWriterGone());
    SeamHubSheetStatus st;
    seam_hub_v1_sheet_status(hub, &st);
    CHECK(st.writing == 0u);
    CHECK(sheets(folder).size() == 1);
    // A later register opens a new sheet (a new file, even within the same
    // second: the name takes a -2 suffix).
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    REQUIRE(waitWriterGone());
    CHECK(sheets(folder).size() == 2);
}

TEST_CASE("the last unregister returns within 2.1 s even if the writer is stuck") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("stuck");
    useFolder(folder);
    const int32_t a = seam_hub_v1_register(hub, "stuck", 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the first pass is done
    seam_hub_test_stall_writer(5000);
    const auto t0 = std::chrono::steady_clock::now();
    seam_hub_v1_unregister(hub, a);
    const double ms = msSince(t0);
    CHECK(ms < 2100.0);
    CHECK(ms > 1500.0);   // it did wait for the leaves line, up to the bound
    // A register racing the stuck writer does not wait on the disk either.
    const auto t1 = std::chrono::steady_clock::now();
    const int32_t b = seam_hub_v1_register(hub, "stuck", 0, 0);
    CHECK(msSince(t1) < 100.0);
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
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    REQUIRE(seam_hub_test_writer_alive() == 1);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(seam_hub_test_teardown() == 1);
    CHECK(msSince(t0) < 300.0);
    CHECK(seam_hub_test_writer_alive() == 0);
    const auto files = sheets(folder);
    REQUIRE(files.size() == 1);
    const auto lines = fileLines(files[0]);
    REQUIRE(!lines.empty());
    CHECK(verbOf(lines.back()) == "leaves");
}

TEST_CASE("quit with a stuck writer returns within 2.1 s") {
    Cleanup cleanup;
    SeamHub* hub = seam_hub_v1_get();
    const fs::path folder = freshDir("quitstuck");
    useFolder(folder);
    seam_hub_test_set_grace_ms(10000);
    // Members gone first, so nothing is left held after the detached writer
    // finishes (a held line would land in the next case's sheet).
    seam_hub_v1_unregister(hub, seam_hub_v1_register(hub, "strx", 0, 0));
    seam_hub_test_stall_writer(5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // a lingering tick is now stuck
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(seam_hub_test_teardown() == 0);   // detached, not joined
    const double ms = msSince(t0);
    CHECK(ms < 2100.0);
    CHECK(ms > 1500.0);
    // Released, the detached writer finishes its pass under stop, closes the
    // sheet and exits; the hub it touches was never destroyed.
    seam_hub_test_stall_writer(0);
    REQUIRE(waitWriterGone());
    // And the hub works again: a new session starts a new writer.
    const int32_t a = seam_hub_v1_register(hub, "strx", 0, 0);
    CHECK(seam_hub_test_writer_alive() == 1);
    seam_hub_test_set_grace_ms(kTestGraceMs);   // before the unregister that starts the grace
    seam_hub_v1_unregister(hub, a);
}
