# 2048 Terminal Game

A feature-rich 2048 game implementation in C++ for terminal.

## Features

- Classic 2048 gameplay with beautiful terminal interface
- I assistant with move evaluation and auto-play mode
- Practice mode with custom board setup and undo function
- Cross-platform support (Windows/macOS/Linux)
- Save/load game progress
- Real-time score tracking and high score recording

## Compilation & Running

### Windows (Visual Studio)
```bash
# 1. Open Visual Studio and create a new C++ Console Application
# 2. Add both 2048.h and 2048.cpp to your project
# 3. Build and run (F5)

# Or compile from command line (Developer Command Prompt):
cl /EHsc /std:c++11 2048src.cpp
```

### Mac/Linux
```bash
# Compile with g++
g++ -std=c++11 -O2 2048src.cpp -o 2048src

# Run the game
./2048src
```
Note: The game must be run in a terminal with ANSI color support.

## Basic Controls
- W/A/S/D or Arrow Keys - Move tiles

- R - Restart game

- Q - Quit game

- I - Toggle AI evaluation display

- 0 - Enable/disable AI auto-play mode

- H - Show help menu

## Requirements

- Terminal size: Minimum 110×70 characters

- UTF-8 encoding support

- ANSI escape sequence support