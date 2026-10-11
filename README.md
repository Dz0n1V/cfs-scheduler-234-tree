# CFS Scheduler Simulator (2-3-4 Tree)

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Build](https://img.shields.io/badge/build-make-lightgrey.svg)
![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen.svg)

A C++17 simulator of a simplified **Completely Fair Scheduler**, the process scheduler used by the Linux kernel.
The run queue is a **2-3-4 tree** that is implemented through its isomorphism with **red-black trees**, so every
operation is `O(log n)` and the tree can be printed both as a 2-3-4 tree and as the underlying binary red-black tree.

The project is dependency-free (standard library only), header-only at its core, and ships with an
interactive console menu and an automated test suite that checks the tree and the scheduler against an independent reference model.

## Features

- **2-3-4 tree on top of a red-black tree** with top-down insertion (4-nodes are split on the way down) and
  red-black deletion (borrow / merge in 2-3-4 terms)
- **Scheduler simulation**: step by step, or run to completion with the queue printed after every step into a log file
- **Two tree views**: `operator<<` prints the 2-3-4 tree; a second view prints the binary red-black tree with node colours
- **Interactive menu**: load processes from a file, add / inspect / modify / remove processes, search, change the time slice
- **Robust input handling**: validated parameters, tolerant file parser (blank lines, Windows line endings, names with spaces,
  bad lines are reported and skipped), clean exit when stdin is closed
- **Verified**: invariant checker, randomised tests against a reference implementation, AddressSanitizer / UBSan clean

## How it works

### 2-3-4 tree ⇄ red-black tree

A red-black tree is just a 2-3-4 tree drawn as a binary tree. Each black node, together with its red children,
forms one 2-3-4 node:

| 2-3-4 node | Red-black representation |
|------------|--------------------------|
| 2-node `[ b ]` | black node, no red children |
| 3-node `[ a b ]` | black node with **one** red child (left or right) |
| 4-node `[ a b c ]` | black node with **two** red children |

This gives the tree its balance for free: all leaves of the 2-3-4 tree are at the same depth, which in red-black
terms means every root-to-leaf path has the same number of black nodes.

| Operation | 2-3-4 view | Red-black implementation | Cost |
|-----------|------------|--------------------------|------|
| Insert | split every 4-node met on the way down, then add the key to a leaf | colour flip, plus rotations if the parent is red | `O(log n)` |
| Delete | borrow a key from a sibling or merge with it | standard red-black delete fix-up | `O(log n)` |
| Take the next process | smallest key = leftmost node | walk down the left spine | `O(log n)` |
| Search by waiting + execution time | binary search on the waiting time, then filter | ordered traversal that only descends where equal keys can be | `O(log n + k)` |
| Add time to every waiting process | uniform shift of all keys, order is unchanged | one tree traversal | `O(n)` |

### Scheduling model

Every process has a name (up to 256 characters), a **time to complete**, and a **maximum waiting time**.
When it enters the queue its **waiting time** and **execution time** are both `0`. The queue is ordered by
waiting time (ties are broken by PID, which makes the order strict and deterministic).

One scheduling step with time slice `S`:

1. Remove the **leftmost** process (smallest waiting time) from the tree.
2. Run it for `min(S, remaining time)` and add that amount to its waiting time and execution time.
3. Add the same amount to the waiting time of every process still in the tree. Any process whose waiting time reached
   or passed its maximum waiting time has the maximum **subtracted**, is removed and re-inserted at its new position.
4. If the process has not reached its time to complete, insert it back into the tree; otherwise it has finished.

The simulation ends when the queue is empty.

## Getting started

### Prerequisites

Any C++17 compiler (developed and tested with GCC 13 on Linux) and `make`. No third-party libraries.

### Build and run

```bash
git clone https://github.com/Dz0n1V/cfs-scheduler-234-tree.git
cd cfs-scheduler-234-tree

make                      # builds build/cfs_scheduler
./build/cfs_scheduler     # starts the interactive menu
```

Without `make`:

```bash
g++ -std=c++17 -O2 -Isrc src/main.cpp -o cfs_scheduler
```

### Run the tests

```bash
make test        # unit, randomised and end-to-end tests
make sanitize    # the same tests under AddressSanitizer + UndefinedBehaviorSanitizer
```

## Usage

### Menu

```
=== CFS Scheduler Simulator (2-3-4 tree)  |  queue: 5 process(es), time slice: 5 ===
 1. Load processes from a file
 2. Add a process manually
 3. Print the queue as a 2-3-4 tree
 4. Print the queue as a red-black tree
 5. Search by waiting time and execution time
 6. Inspect / modify a process
 7. Remove a process
 8. Run one simulation step
 9. Run the full simulation (log to file)
10. Set the time slice
11. Clear the queue
 0. Exit
```

Processes are identified by a PID that is assigned in insertion order, starting at 1.
Every node is printed as `name#PID(current waiting time / max waiting time)`.

### Input file format

One process per line: `<name> <time to complete> <max waiting time>`. The name may contain spaces
(the last two tokens are the numbers). Ready-made workloads are in [`examples/`](examples/):

```
editor 20 40
compiler 35 60
backup 50 25
browser 15 30
shell 10 20
```

| File | Description |
|------|-------------|
| `examples/workload_demo.txt` | 5 processes, small enough to follow by hand |
| `examples/workload_long_jobs.txt` | 26 processes with long run times (up to 652 units) |
| `examples/workload_short_jobs.txt` | 26 processes with short run times and tight waiting limits |

### Example: the two tree views

After loading `examples/workload_demo.txt`, options 3 and 4 print the same queue in both forms:

```
--- Queue (2-3-4 tree) ---
[ compiler#2(0/60) ]
    [ editor#1(0/40) ]
    [ backup#3(0/25) browser#4(0/30) shell#5(0/20) ]

--- Queue (red-black tree) ---
[B] compiler#2(0/60)
    L: [B] editor#1(0/40)
    R: [B] browser#4(0/30)
        L: [R] backup#3(0/25)
        R: [R] shell#5(0/20)
```

The 4-node `[ backup browser shell ]` is the black node `browser` with two red children.

### Example: simulation log

Option 9 writes every step and the resulting tree to a file (here with a time slice of 5):

```
=== Step 4 (t = 15 -> 20) ===
Running editor#1 for 5 | exec 15 -> 20 / 20 | wait 15 -> 20
  ! shell#5 reached its max waiting time (20), waiting time reset to 0
  -> editor#1 finished.
Queue after step 4:
[ backup#3(20/25) ]
    [ shell#5(0/20) compiler#2(20/60) ]
    [ browser#4(20/30) ]
```

## Testing

`tests/test_scheduler.cpp` is a dependency-free test program. It verifies:

- **Tree invariants** after every operation: binary-search order, black root, no red node with a red child,
  equal black height on all paths, consistent parent links, correct size
- **Insertion** in ascending, descending, and heavily duplicated key orders; **deletion** in random order
- **Randomised operation sequences** (insert / erase / pop-min / bulk update) compared against a sorted-vector reference
- **Search** by waiting and execution time compared against a brute-force scan
- **2-3-4 output shape**: every node has 1-3 keys and all leaves are at the same depth
- **Scheduler correctness**: complete simulations of the example workloads (five time slices each) and of random
  workloads are compared step by step with a naive reference model; every process receives exactly its time to complete
- **File parsing** edge cases and the process add / modify / remove API

## Project structure

```
.
├── src/
│   ├── main.cpp          # interactive console menu
│   ├── scheduler.hpp     # scheduling logic, file loading, step / full simulation
│   ├── tree234.hpp       # 2-3-4 tree on a red-black tree, printing, invariant checker
│   ├── process.hpp       # process record, ordering, validation
│   └── text_utils.hpp    # trimming and strict integer parsing
├── tests/
│   └── test_scheduler.cpp
├── examples/             # sample workloads
├── Makefile
└── README.md
```

## Design notes

- **Deterministic ordering.** Keys are `(waiting time, PID)`, so processes with equal waiting times never collide and
  every run of the same input produces the same schedule.
- **Scheduling rule is applied literally.** The running process also gains waiting time, so relative to the rest of the
  queue it keeps its place on the left until other processes hit their maximum waiting time and are reset to a lower
  value. That reset is what hands the CPU to the others, which is why the logs show a process running several slices
  in a row.
- **Limits.** Times are `int` and must be between 1 and 100,000,000 (the time slice as well), which keeps all arithmetic
  well inside the integer range.
- **Simplified model.** This is an educational simulation inspired by Linux CFS, not a reimplementation of it:
  there is a single CPU, no priorities or nice values, and time is discrete.

## Possible extensions

- Multiple CPU cores with one run queue each
- Process priorities that scale the time slice
- Importing / exporting workloads as CSV
- Visualising the queue as Graphviz output

## Author

[Dz0n1V](https://github.com/Dz0n1V)
