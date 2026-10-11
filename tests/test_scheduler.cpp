// Self-contained tests (no framework). Run from the repository root: `make test`.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "scheduler.hpp"

using namespace cfs;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(cond)) {                                                                   \
            ++g_failures;                                                                \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                                \
    } while (0)

#define CHECK_VALID(tree)                                                                \
    do {                                                                                 \
        std::string why;                                                                 \
        ++g_checks;                                                                      \
        if (!(tree).validate(&why)) {                                                    \
            ++g_failures;                                                                \
            std::cerr << "  INVALID TREE " << __FILE__ << ":" << __LINE__ << "  " << why << "\n"; \
        }                                                                                \
    } while (0)

static Process make(int id, int waiting, int exec = 0) {
    Process p(id, "P" + std::to_string(id), 1000, 500);
    p.waiting_time = waiting;
    p.execution_time = exec;
    return p;
}

static bool same_order(const std::vector<Process>& a, const std::vector<Process>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].waiting_time != b[i].waiting_time) return false;
    return true;
}

// ---------------------------------------------------------------- tree tests
static void test_insert_patterns() {
    std::cout << "insert: ascending / descending / many equal keys\n";
    for (int pattern = 0; pattern < 3; ++pattern) {
        Tree234 t;
        for (int i = 1; i <= 600; ++i) {
            const int w = pattern == 0 ? i : pattern == 1 ? 1000 - i : i % 7;  // pattern 2: heavy ties
            t.insert(make(i, w));
            CHECK_VALID(t);
        }
        CHECK(t.size() == 600);
        const auto v = t.to_vector();
        CHECK(std::is_sorted(v.begin(), v.end()));
    }
}

static void test_erase_invariants() {
    std::cout << "erase: random order keeps all invariants\n";
    std::mt19937 rng(42);
    Tree234 t;
    std::vector<Process> all;
    for (int i = 1; i <= 800; ++i) {
        all.push_back(make(i, static_cast<int>(rng() % 50)));
        t.insert(all.back());
    }
    std::shuffle(all.begin(), all.end(), rng);
    for (const Process& p : all) {
        CHECK(t.erase(p));
        CHECK_VALID(t);
    }
    CHECK(t.empty());
    CHECK(!t.erase(make(1, 0)));
}

static void test_random_ops_vs_reference() {
    std::cout << "random insert/erase/pop_min/bulk-update vs sorted-vector reference\n";
    std::mt19937 rng(7);
    Tree234 t;
    std::vector<Process> ref;
    int next_id = 1;
    for (int op = 0; op < 6000; ++op) {
        const int r = static_cast<int>(rng() % 10);
        if (r < 5 || ref.empty()) {
            Process p = make(next_id++, static_cast<int>(rng() % 40));
            t.insert(p);
            ref.push_back(p);
        } else if (r < 7) {
            const std::size_t i = rng() % ref.size();
            CHECK(t.erase_by_id(ref[i].id));
            ref.erase(ref.begin() + static_cast<long>(i));
        } else if (r < 9) {
            const Process p = t.pop_min();
            const auto it = std::min_element(ref.begin(), ref.end());
            CHECK(p.id == it->id);
            ref.erase(it);
        } else {
            const int inc = 1 + static_cast<int>(rng() % 5);
            t.add_waiting_time(inc);
            for (Process& p : ref) p.waiting_time += inc;
        }
        std::sort(ref.begin(), ref.end());
        if (op % 50 == 0) {
            CHECK_VALID(t);
            CHECK(same_order(t.to_vector(), ref));
            CHECK(t.size() == ref.size());
        }
    }
    CHECK_VALID(t);
    CHECK(same_order(t.to_vector(), ref));
}

static void test_search() {
    std::cout << "search by (waiting time, execution time)\n";
    std::mt19937 rng(11);
    Tree234 t;
    std::vector<Process> ref;
    for (int i = 1; i <= 500; ++i) {
        Process p = make(i, static_cast<int>(rng() % 20), static_cast<int>(rng() % 5));
        t.insert(p);
        ref.push_back(p);
    }
    for (int w = -1; w <= 21; ++w) {
        for (int e = 0; e <= 5; ++e) {
            std::vector<int> expected;
            for (const Process& p : ref)
                if (p.waiting_time == w && p.execution_time == e) expected.push_back(p.id);
            std::sort(expected.begin(), expected.end());
            std::vector<int> got;
            for (const Process& p : t.search(w, e)) got.push_back(p.id);
            CHECK(got == expected);
        }
    }
}

// Parses the 2-3-4 view and checks that it really is a 2-3-4 tree:
// every node holds 1..3 keys, all leaves sit at the same depth, key count == size.
static void test_234_view_shape() {
    std::cout << "2-3-4 print: 1-3 keys per node, all leaves at the same depth\n";
    std::mt19937 rng(5);
    for (int n : {1, 2, 3, 4, 5, 10, 33, 100, 257, 500}) {
        Tree234 t;
        for (int i = 1; i <= n; ++i) t.insert(make(i, static_cast<int>(rng() % 100)));
        std::ostringstream os;
        os << t;
        std::istringstream in(os.str());
        std::vector<int> depth, keys;
        std::string line;
        while (std::getline(in, line)) {
            const auto indent = line.find_first_not_of(' ');
            depth.push_back(static_cast<int>(indent / 4));
            const int k = static_cast<int>(std::count(line.begin(), line.end(), '#'));
            keys.push_back(k);
        }
        int total = 0;
        bool ok = true;
        int leaf_depth = -1;
        for (std::size_t i = 0; i < depth.size(); ++i) {
            total += keys[i];
            if (keys[i] < 1 || keys[i] > 3) ok = false;
            const bool leaf = (i + 1 == depth.size()) || depth[i + 1] <= depth[i];
            if (leaf) {
                if (leaf_depth < 0) leaf_depth = depth[i];
                else if (leaf_depth != depth[i]) ok = false;
            }
        }
        CHECK(ok);
        CHECK(total == n);

        std::ostringstream rb;
        rb << t.view(TreeFormat::RedBlack);
        const std::string rb_text = rb.str();
        CHECK(static_cast<int>(std::count(rb_text.begin(), rb_text.end(), '\n')) == n);
    }
    Tree234 empty;
    std::ostringstream os;
    os << empty;
    CHECK(os.str() == "(empty)\n");
}

static void test_clear_and_reuse() {
    std::cout << "clear() leaves a reusable empty tree\n";
    Tree234 t;
    for (int i = 1; i <= 50; ++i) t.insert(make(i, i));
    t.clear();
    CHECK(t.empty() && t.size() == 0);
    CHECK_VALID(t);
    t.insert(make(1, 3));
    CHECK(t.size() == 1);
    CHECK_VALID(t);
    bool threw = false;
    Tree234 e;
    try { e.pop_min(); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
}

// ----------------------------------------------------------- scheduler tests
// Naive O(n^2) implementation of the same rules, used as an oracle.
struct ReferenceStep {
    int pid;
    int ran;
    bool finished;
    std::vector<int> boosted;
};

static std::vector<ReferenceStep> reference_simulation(std::vector<Process> q, int slice) {
    std::vector<ReferenceStep> steps;
    while (!q.empty()) {
        auto it = std::min_element(q.begin(), q.end());
        Process cur = *it;
        q.erase(it);
        const int run = std::min(slice, cur.time_to_complete - cur.execution_time);
        cur.execution_time += run;
        cur.waiting_time += run;
        ReferenceStep s{cur.id, run, false, {}};
        for (Process& p : q) {
            p.waiting_time += run;
            if (p.waiting_time >= p.max_waiting_time) {
                p.waiting_time -= p.max_waiting_time;
                s.boosted.push_back(p.id);
            }
        }
        std::sort(s.boosted.begin(), s.boosted.end());
        s.finished = cur.execution_time >= cur.time_to_complete;
        if (!s.finished) q.push_back(cur);
        steps.push_back(s);
    }
    return steps;
}

static void compare_with_reference(Scheduler& sch, const std::string& label) {
    std::vector<Process> initial = sch.queue().to_vector();
    const auto expected = reference_simulation(initial, sch.time_slice());

    std::map<int, int> executed;  // pid -> total CPU time received
    std::size_t i = 0;
    bool all_match = true;
    while (auto r = sch.step()) {
        CHECK_VALID(sch.queue());
        if (i >= expected.size()) { all_match = false; break; }
        std::vector<int> boosted;
        for (const Process& p : r->boosted) boosted.push_back(p.id);
        std::sort(boosted.begin(), boosted.end());
        const ReferenceStep& e = expected[i];
        if (r->before.id != e.pid || r->executed_for != e.ran || r->finished != e.finished || boosted != e.boosted)
            all_match = false;
        executed[r->before.id] += r->executed_for;
        ++i;
    }
    CHECK(all_match);
    CHECK(i == expected.size());
    CHECK(sch.empty());
    for (const Process& p : initial) CHECK(executed[p.id] == p.time_to_complete);  // everyone got exactly what it needed
    std::cout << "  " << label << ": " << i << " steps match the reference model\n";
}

static void test_example_files() {
    std::cout << "full simulation on the example workloads (several time slices)\n";
    for (const char* file : {"examples/workload_long_jobs.txt", "examples/workload_short_jobs.txt"}) {
        for (int slice : {1, 3, 5, 8, 50}) {
            Scheduler sch(slice);
            std::ostringstream warnings;
            const LoadResult r = sch.load_from_file(file, warnings);
            CHECK(r.file_opened);
            CHECK(r.loaded == 26 && r.skipped == 0);
            CHECK_VALID(sch.queue());
            compare_with_reference(sch, std::string(file) + " slice=" + std::to_string(slice));
        }
    }
}

static void test_random_workloads() {
    std::cout << "random workloads vs reference model\n";
    std::mt19937 rng(2024);
    for (int round = 0; round < 25; ++round) {
        Scheduler sch(1 + static_cast<int>(rng() % 12));
        const int n = 1 + static_cast<int>(rng() % 60);
        for (int i = 0; i < n; ++i)
            sch.add_process("job" + std::to_string(i), 1 + static_cast<int>(rng() % 300), 1 + static_cast<int>(rng() % 400));
        compare_with_reference(sch, "round " + std::to_string(round));
    }
}

static void test_file_parsing() {
    std::cout << "file parsing: blank lines, CRLF, names with spaces, bad lines\n";
    const char* path = "build/_parse_test.txt";
    {
        std::ofstream f(path, std::ios::binary);
        f << "A 10 20\r\n"
          << "\n"
          << "  My Long Name   30   40  \n"
          << "bad line\n"
          << "NoNumbers x y\n"
          << "Zero 0 10\n"          // time to complete must be >= 1
          << "NegWait 5 -3\n"       // max waiting must be >= 1
          << "OnlyOne 7\n"
          << std::string(257, 'x') << " 5 5\n"   // name too long
          << std::string(256, 'y') << " 5 5\n"   // longest allowed name
          << "Last 1 1";                          // no trailing newline
    }
    Scheduler sch;
    std::ostringstream warnings;
    const LoadResult r = sch.load_from_file(path, warnings);
    CHECK(r.file_opened);
    CHECK(r.loaded == 4);
    CHECK(r.skipped == 6);
    CHECK_VALID(sch.queue());
    const auto v = sch.queue().to_vector();
    bool has_spaced_name = false;
    for (const Process& p : v)
        if (p.name == "My Long Name" && p.time_to_complete == 30 && p.max_waiting_time == 40) has_spaced_name = true;
    CHECK(has_spaced_name);
    std::remove(path);
    Scheduler other;
    CHECK(!other.load_from_file("does/not/exist.txt", warnings).file_opened);
}

static void test_process_management() {
    std::cout << "add / modify / remove through the scheduler API\n";
    Scheduler sch(4);
    const int a = sch.add_process("A", 20, 50);
    const int b = sch.add_process("B", 30, 60);
    const int c = sch.add_process("C", 40, 70);
    CHECK(a == 1 && b == 2 && c == 3);

    bool threw = false;
    try { sch.add_process("", 10, 10); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { sch.add_process("X", 0, 10); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(sch.size() == 3);

    // Change the waiting time (the tree key): the process must move to the right place.
    Process p = *sch.find(c);
    p.waiting_time = 0;
    p.execution_time = 3;
    std::string err;
    CHECK(sch.replace_process(p, err));
    CHECK_VALID(sch.queue());
    CHECK(sch.search(0, 3).size() == 1 && sch.search(0, 3)[0].id == c);

    p.execution_time = p.time_to_complete;  // not allowed: it would already be finished
    CHECK(!sch.replace_process(p, err) && !err.empty());
    Process ghost = p;
    ghost.id = 99;
    CHECK(!sch.replace_process(ghost, err));

    CHECK(sch.remove_process(b));
    CHECK(!sch.remove_process(b));
    CHECK(sch.size() == 2);
    CHECK_VALID(sch.queue());

    CHECK(!sch.set_time_slice(0) && !sch.set_time_slice(-3));
    CHECK(sch.set_time_slice(7) && sch.time_slice() == 7);

    sch.clear();
    CHECK(sch.empty());
    CHECK(sch.add_process("again", 5, 5) == 1);  // PIDs restart after a clear
    CHECK(sch.step().has_value());
}

static void test_log_output() {
    std::cout << "full-simulation log contains a tree per step\n";
    Scheduler sch(10);
    sch.add_process("A", 25, 100);
    sch.add_process("B", 12, 100);
    std::ostringstream log;
    const int steps = sch.run_to_completion(log, TreeFormat::TwoThreeFour);
    CHECK(steps == 5);  // A: 10+10+5, B: 10+2
    const std::string text = log.str();
    std::size_t count = 0;
    for (std::size_t pos = text.find("Queue after step"); pos != std::string::npos; pos = text.find("Queue after step", pos + 1))
        ++count;
    CHECK(count == static_cast<std::size_t>(steps));
    CHECK(text.find("finished.") != std::string::npos);
}

int main() {
    test_insert_patterns();
    test_erase_invariants();
    test_random_ops_vs_reference();
    test_search();
    test_234_view_shape();
    test_clear_and_reuse();
    test_example_files();
    test_random_workloads();
    test_file_parsing();
    test_process_management();
    test_log_output();

    std::cout << "\n" << g_checks << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
