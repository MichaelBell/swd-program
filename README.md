# SWD Programmer

Load a program onto a target RP2040 using another Pico, without needing OpenOCD.

To use, turn the elf for your program into a header file using:
```
objdump -s your_program.elf >dump.txt
python3 convert_elf.py > your_program.h
```
then include that header in `main.cpp`.
