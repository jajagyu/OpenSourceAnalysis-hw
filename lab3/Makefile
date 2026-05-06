TOP=..
include $(TOP)/lab3/common.mk

.DEFAULT_GOAL := all

LDLIBS=-lpthread
GTEST_OBJS=gtest-all.o

TEST_TARGET=db_test
BENCH_TARGET=db_bench

TEST_SRCS=db_test.cc memdb.cc skiplist.cc sstable.cc bloom_filter.cc compaction.cc
BENCH_SRCS=db_bench.cc memdb.cc skiplist.cc sstable.cc bloom_filter.cc compaction.cc

TEST_OBJS=$(TEST_SRCS:.cc=.o)
BENCH_OBJS=$(BENCH_SRCS:.cc=.o)

all: $(GTEST_OBJS) $(TEST_TARGET) $(BENCH_TARGET)

$(TEST_TARGET): $(TEST_OBJS) $(GTEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BENCH_TARGET): $(BENCH_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TEST_OBJS) $(BENCH_OBJS) $(GTEST_OBJS) $(TEST_TARGET) $(BENCH_TARGET) memdb_test memdb_bench

.PHONY: all clean
