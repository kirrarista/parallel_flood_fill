# By Kirill Aristarkhov and Brandon Katz for CS140 S22 
# Makefile for flood fill program
all: flood

flood: flood.cpp
	g++ -Wall -std=c++11 -o flood flood.cpp -fopenmp -lpthread

clean:
	rm -f flood