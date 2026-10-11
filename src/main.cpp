#include <fstream>
#include <iostream>
#include <string>

#include "scheduler.hpp"
#include "text_utils.hpp"

namespace {

using cfs::Process;
using cfs::Scheduler;
using cfs::TreeFormat;

// Thrown when stdin is closed so the program can exit cleanly.
struct InputClosed {};

std::string read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) throw InputClosed{};
    return cfs::trim(line);
}

int read_int(const std::string& prompt) {
    while (true) {
        int value = 0;
        if (cfs::parse_int(read_line(prompt), value)) return value;
        std::cout << "Please enter a whole number.\n";
    }
}

TreeFormat read_format() {
    while (true) {
        const int choice = read_int("Tree view - 1) 2-3-4 tree, 2) red-black tree: ");
        if (choice == 1) return TreeFormat::TwoThreeFour;
        if (choice == 2) return TreeFormat::RedBlack;
        std::cout << "Please enter 1 or 2.\n";
    }
}

void print_menu(const Scheduler& s) {
    std::cout << "\n=== CFS Scheduler Simulator (2-3-4 tree)  |  queue: " << s.size()
              << " process(es), time slice: " << s.time_slice() << " ===\n"
              << " 1. Load processes from a file\n"
              << " 2. Add a process manually\n"
              << " 3. Print the queue as a 2-3-4 tree\n"
              << " 4. Print the queue as a red-black tree\n"
              << " 5. Search by waiting time and execution time\n"
              << " 6. Inspect / modify a process\n"
              << " 7. Remove a process\n"
              << " 8. Run one simulation step\n"
              << " 9. Run the full simulation (log to file)\n"
              << "10. Set the time slice\n"
              << "11. Clear the queue\n"
              << " 0. Exit\n";
}

void print_process_row(const Process& p) {
    std::cout << "  PID " << p.id << "  " << p.name << "  wait " << p.waiting_time << '/' << p.max_waiting_time
              << "  exec " << p.execution_time << '/' << p.time_to_complete << '\n';
}

void do_load(Scheduler& s) {
    const std::string path = read_line("File name: ");
    const cfs::LoadResult r = s.load_from_file(path, std::cout);
    if (!r.file_opened) {
        std::cout << "Could not open '" << path << "'.\n";
        return;
    }
    std::cout << "Loaded " << r.loaded << " process(es)";
    if (r.skipped > 0) std::cout << ", skipped " << r.skipped << " invalid line(s)";
    std::cout << ".\n";
}

void do_add(Scheduler& s) {
    const std::string name = read_line("Process name: ");
    const int ttc = read_int("Time to complete: ");
    const int mwt = read_int("Maximum waiting time: ");
    try {
        const int pid = s.add_process(name, ttc, mwt);
        std::cout << "Added " << name << " with PID " << pid << ".\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "Not added: " << e.what() << '\n';
    }
}

void do_print(const Scheduler& s, TreeFormat format) {
    if (s.empty()) {
        std::cout << "The queue is empty.\n";
        return;
    }
    std::cout << (format == TreeFormat::TwoThreeFour ? "--- Queue (2-3-4 tree) ---" : "--- Queue (red-black tree) ---")
              << "\nLegend: name#PID(current wait/max wait)\n"
              << s.queue().view(format);
}

void do_search(const Scheduler& s) {
    const int w = read_int("Current waiting time: ");
    const int e = read_int("Current execution time: ");
    const auto found = s.search(w, e);
    if (found.empty()) {
        std::cout << "No matching process.\n";
        return;
    }
    std::cout << found.size() << " match(es):\n";
    for (const Process& p : found) print_process_row(p);
}

void do_modify(Scheduler& s) {
    const int pid = read_int("PID: ");
    auto p = s.find(pid);
    if (!p) {
        std::cout << "No process with PID " << pid << ".\n";
        return;
    }
    print_process_row(*p);
    std::cout << "Change: 1) name  2) time to complete  3) max waiting time  4) waiting time  "
                 "5) execution time  0) nothing\n";
    const int field = read_int("Field: ");
    switch (field) {
        case 0: return;
        case 1: p->name = read_line("New name: "); break;
        case 2: p->time_to_complete = read_int("New time to complete: "); break;
        case 3: p->max_waiting_time = read_int("New max waiting time: "); break;
        case 4: p->waiting_time = read_int("New waiting time: "); break;
        case 5: p->execution_time = read_int("New execution time: "); break;
        default: std::cout << "Unknown field.\n"; return;
    }
    std::string error;
    if (s.replace_process(*p, error))
        std::cout << "Updated.\n";
    else
        std::cout << "Not changed: " << error << '\n';
}

void do_remove(Scheduler& s) {
    const int pid = read_int("PID to remove: ");
    std::cout << (s.remove_process(pid) ? "Removed.\n" : "No process with that PID.\n");
}

void do_step(Scheduler& s) {
    const auto report = s.step();
    if (!report) {
        std::cout << "The queue is empty.\n";
        return;
    }
    Scheduler::write_step_report(std::cout, *report);
    do_print(s, TreeFormat::TwoThreeFour);
}

void do_full_simulation(Scheduler& s) {
    if (s.empty()) {
        std::cout << "The queue is empty.\n";
        return;
    }
    const std::string path = read_line("Log file name: ");
    const TreeFormat format = read_format();
    std::ofstream log(path);
    if (!log.is_open()) {
        std::cout << "Could not create '" << path << "'.\n";
        return;
    }
    const int steps = s.run_to_completion(log, format);
    std::cout << "Simulation finished: " << steps << " steps, " << s.clock() << " time units. Log written to "
              << path << ".\n";
}

void do_set_slice(Scheduler& s) {
    const int slice = read_int("New time slice: ");
    if (s.set_time_slice(slice))
        std::cout << "Time slice set to " << slice << ".\n";
    else
        std::cout << "The time slice must be between 1 and " << cfs::kMaxTimeValue << ".\n";
}

}  // namespace

int main() {
    Scheduler scheduler(5);
    try {
        while (true) {
            print_menu(scheduler);
            switch (read_int("Choice: ")) {
                case 1: do_load(scheduler); break;
                case 2: do_add(scheduler); break;
                case 3: do_print(scheduler, TreeFormat::TwoThreeFour); break;
                case 4: do_print(scheduler, TreeFormat::RedBlack); break;
                case 5: do_search(scheduler); break;
                case 6: do_modify(scheduler); break;
                case 7: do_remove(scheduler); break;
                case 8: do_step(scheduler); break;
                case 9: do_full_simulation(scheduler); break;
                case 10: do_set_slice(scheduler); break;
                case 11:
                    scheduler.clear();
                    std::cout << "Queue cleared.\n";
                    break;
                case 0: std::cout << "Goodbye.\n"; return 0;
                default: std::cout << "Unknown option.\n";
            }
        }
    } catch (const InputClosed&) {
        std::cout << "\nInput closed, exiting.\n";
    }
    return 0;
}
