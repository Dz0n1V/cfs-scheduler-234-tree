#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace cfs {

constexpr std::size_t kMaxNameLength = 256;
constexpr int kMaxTimeValue = 100000000;  // keeps all time arithmetic safely inside int

// A process waiting in (or running from) the scheduler's queue.
struct Process {
    int id = 0;                 // unique PID, also the tie-breaker for equal waiting times
    std::string name;
    int time_to_complete = 0;   // total CPU time the process needs
    int max_waiting_time = 0;   // waiting time at which the process is "boosted" back
    int waiting_time = 0;       // current waiting time (the tree key)
    int execution_time = 0;     // CPU time received so far

    Process() = default;
    Process(int pid, std::string n, int ttc, int mwt)
        : id(pid), name(std::move(n)), time_to_complete(ttc), max_waiting_time(mwt) {}

    // Strict total order used by the tree: (waiting_time, id).
    bool operator<(const Process& other) const {
        if (waiting_time != other.waiting_time) return waiting_time < other.waiting_time;
        return id < other.id;
    }
};

// Returns an empty string if the process is valid, otherwise a human-readable reason.
inline std::string validate_process(const Process& p) {
    if (p.name.empty()) return "Process name must not be empty.";
    if (p.name.size() > kMaxNameLength) return "Process name is longer than 256 characters.";
    if (p.time_to_complete <= 0 || p.time_to_complete > kMaxTimeValue)
        return "Time to complete must be between 1 and 100000000.";
    if (p.max_waiting_time <= 0 || p.max_waiting_time > kMaxTimeValue)
        return "Maximum waiting time must be between 1 and 100000000.";
    if (p.waiting_time < 0 || p.waiting_time > kMaxTimeValue)
        return "Waiting time must be between 0 and 100000000.";
    if (p.execution_time < 0 || p.execution_time >= p.time_to_complete)
        return "Execution time must be at least 0 and less than the time to complete.";
    return "";
}

// "name#pid(wait/max_wait)"
inline std::ostream& operator<<(std::ostream& os, const Process& p) {
    return os << p.name << '#' << p.id << '(' << p.waiting_time << '/' << p.max_waiting_time << ')';
}

}  // namespace cfs
