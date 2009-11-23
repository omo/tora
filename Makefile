
CFLAGS = -I./atomic_ops-0.5 -I.
TARGET = ./tora

run: $(TARGET)
	$(TARGET)

$(TARGET): tora.hpp main.cpp tora.cpp
	g++ $(CFLAGS) -o $@ main.cpp tora.cpp

clean:
	-rm $(TARGET)
	-rm .gdb_history

PHONY: run clean