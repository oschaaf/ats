# If tsxs is not in your path, then change this by running
# make TSXS=/path/to/ts/bin/tsxs target
TSXS?=tsxs

%.so: %.cpp
	$(TSXS) -c $< -o $@

all: rate-limiter.so

install: all
	$(TSXS) -i -o rate-limiter.so

clean:
	rm -f *.lo *.so
