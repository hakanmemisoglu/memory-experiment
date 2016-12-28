CXX=g++-6
CXX_FLAGS=-std=c++14 -Wall -Wextra -O2 -march=native

all: benchmark

benchmark: benchmark.cpp
	${CXX} ${CXX_FLAGS} benchmark.cpp -o benchmark

clean:
	rm benchmark
