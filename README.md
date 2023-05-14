# SWD Programmer

Load a program onto a target RP2040 using another Pico, without needing OpenOCD.

To use, turn the elf for your program into a header file using `convert_elf.py`, then include that header in `main.cpp`.
