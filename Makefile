CXX := g++
CXXFLAGS := -O2 -std=c++17 -ggdb

BIN := bin
SRC := src
INCLUDES := -Iinclude
EXE := server client

all: $(EXE)

server:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -o $(BIN)/$@.out $(SRC)/server.cpp

client:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN)/$@.out $(SRC)/client.cpp

clean:
	@-rm $(BIN)/*

run: all
	$(BIN)/server.out -v
