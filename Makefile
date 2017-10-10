#CXX := g++-4.9
CXX := g++
CXXFLAGS := -std=c++14 -Wall -g -pedantic-errors -Werror -O0 \
    						-Wno-init-self \
						-Wno-maybe-uninitialized \
						-Wno-unused-parameter \
						-Wextra \
						-Weffc++
# REPLAY := yes enables a policy that can feed traces to memcached, but it
# requires libmemcached to be linked in as a result. Only enable it if you
# have the library and headers installed.
REPLAY ?= no

LDFLAGS :=

ifeq ($(REPLAY),yes)
LDFLAGS += -lmemcached
else
CXXFLAGS += -DNOREPLAY
endif

HEADERS := $(wildcard src/*.h)
SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp, src/%.o, $(SRCS))

all: lsm-sim

%.o: src/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

lsm-sim: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ -lpython2.7

clean:
	-rm lsm-sim src/*.o

debug: CXXFLAGS += -DDEBUG
debug: lsm-sim
