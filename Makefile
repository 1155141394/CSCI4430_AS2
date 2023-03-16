## CSCI 4430 Advanced Makefile

# How to use this Makefile...
###################
###################
##               ##
##  $ make help  ##
##               ##
###################
###################

CXX = gcc
# TODO For C++ only.
CXXFLAGS = -g -std=c++11 -pedantic

all: miProxy

# TODO Modify source file name for your project.
# For C only.
SOURCES = miProxy.cpp
# For C++ only.
# SOURCES = iPerfer.cpp
iPerfer: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o miProxy

clean:
	rm -rf miProxy *.dSYM

.PHONY: clean