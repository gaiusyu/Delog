cc = g++
CXXFLAGS = -std=c++11 -g -O0 -Wall -mcmodel=medium

TARGETS = THULR Iddiff Numdiff Entropy Elastic

all: $(TARGETS)

THULR: main.o template.o LengthSearch.o
	$(cc) $(CXXFLAGS) -o $@ $^

Iddiff: iddiff.o
	$(cc) $(CXXFLAGS) -o $@ $^

Numdiff: numdiff.o
	$(cc) $(CXXFLAGS) -o $@ $^

Entropy: entropy.o
	$(cc) $(CXXFLAGS) -o $@ $^

Elastic: elastic.o
	$(cc) $(CXXFLAGS) -o $@ $^

.cpp.o:
	$(cc) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(TARGETS)
