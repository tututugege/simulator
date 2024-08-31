CSRC=./*.cpp
CSRC+=./back-end/*.cpp

all: 
	g++ $(CSRC) -O3 -g

clean:
	rm a.out

phony: all clean
