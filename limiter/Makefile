# If tsxs is not in your path, then change this by running
# make TSXS=/path/to/ts/bin/tsxs target

TSXS?=tsxs

%.so: %.cc
	g++ -shared -g -pipe -Wall -Werror -O3 -fpic *.cc -c rate-limiter/*.cpp -c
	g++ -shared -o limiter.so *.o -ltsutil

all: limiter.so

install: all
	$(TSXS) -i -o limiter.so 

clean:
	rm -f *.lo *.so *.o
