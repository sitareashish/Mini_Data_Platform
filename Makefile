CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude
LDFLAGS  = -lpthread

SRCS = src/btree.cpp src/memory_pool.cpp src/schema.cpp \
       src/storage.cpp src/parser.cpp src/executor.cpp src/llm_copilot.cpp

.PHONY: all clean test run

all: minidb minidb_tests

minidb: $(SRCS) src/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

minidb_tests: $(SRCS) tests/test_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: minidb_tests
	./minidb_tests

run: minidb
	./minidb

clean:
	rm -f minidb minidb_tests

# Run demo automatically
demo: minidb
	echo "\demo\n\\quit\n" | ./minidb
