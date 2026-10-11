CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
BUILD    := build
HEADERS  := $(wildcard src/*.hpp)

.PHONY: all test sanitize clean

all: $(BUILD)/cfs_scheduler

$(BUILD)/cfs_scheduler: src/main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

$(BUILD)/test_scheduler: tests/test_scheduler.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

test: $(BUILD)/test_scheduler
	./$(BUILD)/test_scheduler

# Same tests, built with AddressSanitizer + UndefinedBehaviorSanitizer (catches leaks and UB)
sanitize: | $(BUILD)
	$(CXX) -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/test_scheduler.cpp -o $(BUILD)/test_scheduler_asan
	./$(BUILD)/test_scheduler_asan

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
