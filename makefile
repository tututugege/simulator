CSRC=./*.cpp
CSRC+=./back-end/*.cpp

all: 
	g++ $(CSRC) -g

clean:
	rm a.out

phony: all clean
