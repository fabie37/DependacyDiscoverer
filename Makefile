dependencyDiscoverer: dependencyDiscoverer.cpp
	clang++ -Wall -Werror -std=c++17 -g -o dependencyDiscoverer dependencyDiscoverer.cpp -lpthread

clean:
	rm -f *.o dependencyDiscoverer *~
