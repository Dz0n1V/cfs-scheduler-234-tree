#pragma once

#include <algorithm>
#include <fstream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "process.hpp"
#include "text_utils.hpp"
#include "tree234.hpp"

namespace cfs {

// What happened during one scheduling step.
struct StepReport {
    int step_number = 0;
    int clock_before = 0;
    int clock_after = 0;
    Process before;               // process as it was when picked
    Process after;                // process after running
    int executed_for = 0;         // CPU time granted in this step
    bool finished = false;
    std::vector<Process> boosted; // processes whose waiting time hit the maximum (state after reset)
};

struct LoadResult {
    int loaded = 0;
    int skipped = 0;
    bool file_opened = false;
};

class Scheduler {
public:
    explicit Scheduler(int time_slice = 5) {
        if (!set_time_slice(time_slice)) throw std::invalid_argument("invalid time slice");
    }

    // ---------------------------------------------------------------- settings
    int time_slice() const { return time_slice_; }

    bool set_time_slice(int slice) {
        if (slice <= 0 || slice > kMaxTimeValue) return false;
        time_slice_ = slice;
        return true;
    }

    // ------------------------------------------------------------- queue access
    const Tree234& queue() const { return queue_; }
    std::size_t size() const { return queue_.size(); }
    bool empty() const { return queue_.empty(); }
    int clock() const { return clock_; }
    int steps_done() const { return step_count_; }

    // Destroys the queue and starts over (PIDs and the clock restart too).
    void clear() {
        queue_.clear();
        next_pid_ = 1;
        clock_ = 0;
        step_count_ = 0;
    }

    // ----------------------------------------------------------- process admin
    // Adds a new process with waiting and execution time 0. Returns its PID.
    // Throws std::invalid_argument if the parameters are not valid.
    int add_process(const std::string& name, int time_to_complete, int max_waiting_time) {
        Process p(next_pid_, name, time_to_complete, max_waiting_time);
        const std::string error = validate_process(p);
        if (!error.empty()) throw std::invalid_argument(error);
        queue_.insert(p);
        return next_pid_++;
    }

    // Reads "name time_to_complete max_waiting_time" lines. Names may contain spaces; the
    // last two whitespace-separated tokens are the numbers. Invalid lines are reported on
    // `warnings` and skipped.
    LoadResult load_from_file(const std::string& path, std::ostream& warnings) {
        LoadResult result;
        std::ifstream in(path);
        if (!in.is_open()) return result;
        result.file_opened = true;

        std::string line;
        int line_no = 0;
        while (std::getline(in, line)) {
            ++line_no;
            line = trim(line);
            if (line.empty()) continue;
            std::string name;
            int ttc = 0, mwt = 0;
            if (!parse_process_line(line, name, ttc, mwt)) {
                warnings << "  line " << line_no << ": expected \"<name> <time to complete> <max waiting time>\" - skipped\n";
                ++result.skipped;
                continue;
            }
            try {
                add_process(name, ttc, mwt);
                ++result.loaded;
            } catch (const std::invalid_argument& e) {
                warnings << "  line " << line_no << ": " << e.what() << " - skipped\n";
                ++result.skipped;
            }
        }
        return result;
    }

    std::optional<Process> find(int pid) const { return queue_.find_by_id(pid); }

    // Processes whose current waiting time AND execution time both match.
    std::vector<Process> search(int waiting_time, int execution_time) const {
        return queue_.search(waiting_time, execution_time);
    }

    bool remove_process(int pid) { return queue_.erase_by_id(pid); }

    // Replaces the stored process with the same PID by `updated` (re-inserting it, since the
    // waiting time is the tree key). Returns false and fills `error` on failure.
    bool replace_process(const Process& updated, std::string& error) {
        const auto current = queue_.find_by_id(updated.id);
        if (!current) {
            error = "No process with PID " + std::to_string(updated.id) + ".";
            return false;
        }
        error = validate_process(updated);
        if (!error.empty()) return false;
        queue_.erase(*current);
        queue_.insert(updated);
        return true;
    }

    // -------------------------------------------------------------- simulation
    // Runs one scheduling step:
    //   1. take the leftmost process out of the tree,
    //   2. give it min(time slice, remaining time) and add that to its waiting and execution time,
    //   3. add the same amount to every waiting process; those that reached their maximum
    //      waiting time have it subtracted and are re-inserted,
    //   4. put the process back unless it has finished.
    // Returns std::nullopt if the queue is empty.
    std::optional<StepReport> step() {
        if (queue_.empty()) return std::nullopt;

        StepReport report;
        Process current = queue_.pop_min();
        report.before = current;

        const int remaining = current.time_to_complete - current.execution_time;
        const int run_for = std::min(time_slice_, remaining);
        current.execution_time += run_for;
        current.waiting_time += run_for;

        for (Process& p : queue_.add_waiting_time(run_for)) {
            if (!queue_.erase(p)) throw std::logic_error("scheduler: boosted process not found in queue");
            p.waiting_time -= p.max_waiting_time;
            queue_.insert(p);
            report.boosted.push_back(p);
        }

        report.finished = current.execution_time >= current.time_to_complete;
        if (!report.finished) queue_.insert(current);

        report.after = current;
        report.executed_for = run_for;
        report.step_number = ++step_count_;
        report.clock_before = clock_;
        clock_ += run_for;
        report.clock_after = clock_;
        return report;
    }

    // Runs until the queue is empty, writing every step and the resulting tree to `log`.
    // Returns the number of steps executed.
    int run_to_completion(std::ostream& log, TreeFormat format) {
        const int first_step = step_count_;
        log << "Initial queue:\n" << queue_.view(format) << '\n';
        while (auto report = step()) {
            write_step_report(log, *report);
            log << "Queue after step " << report->step_number << ":\n" << queue_.view(format) << '\n';
        }
        log << "All processes finished. Total simulated time: " << clock_ << ".\n";
        return step_count_ - first_step;
    }

    static void write_step_report(std::ostream& os, const StepReport& r) {
        os << "=== Step " << r.step_number << " (t = " << r.clock_before << " -> " << r.clock_after << ") ===\n";
        os << "Running " << r.before.name << '#' << r.before.id << " for " << r.executed_for
           << " | exec " << r.before.execution_time << " -> " << r.after.execution_time << " / "
           << r.after.time_to_complete << " | wait " << r.before.waiting_time << " -> "
           << r.after.waiting_time << '\n';
        for (const Process& p : r.boosted)
            os << "  ! " << p.name << '#' << p.id << " reached its max waiting time (" << p.max_waiting_time
               << "), waiting time reset to " << p.waiting_time << '\n';
        if (r.finished)
            os << "  -> " << r.after.name << '#' << r.after.id << " finished.\n";
        else
            os << "  -> " << r.after.name << '#' << r.after.id << " returned to the queue.\n";
    }

private:
    Tree234 queue_;
    int time_slice_ = 5;
    int next_pid_ = 1;
    int clock_ = 0;
    int step_count_ = 0;

    // Splits "some name 120 300" into name / 120 / 300 (name may contain spaces).
    static bool parse_process_line(const std::string& line, std::string& name, int& ttc, int& mwt) {
        const char* ws = " \t";
        const auto end2 = line.find_last_not_of(ws);
        const auto start2 = line.find_last_of(ws, end2);
        if (start2 == std::string::npos) return false;
        if (!parse_int(line.substr(start2 + 1, end2 - start2), mwt)) return false;

        const auto end1 = line.find_last_not_of(ws, start2);
        if (end1 == std::string::npos) return false;
        const auto start1 = line.find_last_of(ws, end1);
        if (start1 == std::string::npos) return false;
        if (!parse_int(line.substr(start1 + 1, end1 - start1), ttc)) return false;

        name = trim(line.substr(0, start1));
        return !name.empty();
    }
};

}  // namespace cfs
