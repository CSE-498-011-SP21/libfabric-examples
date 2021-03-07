CXX=gpp
#CPPFLAGS=-lstdc++ -lfabric -O3
CPPFLAGS=-Wall -g

SOURCES := $(shell ls *.cpp)

APPS := $(shell echo Echo)

.PHONY: all
all: $(APPS)

#default rule for all apps
% : %.cpp
	$(CXX) $(CPPFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -f $(APPS)