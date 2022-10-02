# chess and chess_pengy

## chess features

- Engine + GUI (chess), and
- Engine-only (chessx, to be used with XBOARD or WINBOARD)
- 10x10 bytes board representation
- Entire board copy at each move, so move undo is very simple
- negamax search with alpha beta pruning
- Iterative deepening
- PVS (Principle Variation Search)
- PV/killer move ordering
- Evaluation pruning
- Futility prunning
- Opening book

## chess_pengy additional features

- Transposition table (using Pengy hash instead of Zobrist hash)

## Pre-requisites to build

Under Windows, msys64 and MINGW64 must be installed.
Under Linux, gcc must be installed

The GUI uses the SDL2 graphical library

## Building an engine

To build any of these two engines, use build.bat on Windows or build.sh on Linux with as argument the engine name ("chess" or "chess_pengy").
The script will build the two engine variants: with a GUI and GUI-less ('x' added to name end).

Example on Windows:
build chess
build chess_pengy

Example on Linux:
./build chess
./build chess_pengy

## Using the engine version that includes the GUI

On Linux, the executable needs resources grouped in the 'resources' folder
On Windows, the executable needs, in addition to the 'resources' folder, to have access to the 4 following DLLs:

- SDL2.dll
- SDL2_image.dll
- SDL2_ttf.dll
- libfreetype-6.dll

These DLLs can be in the same directory as the program or in a directory listed in the PATH environment variable

Use the mouse to move a piece, use the left arrow and right arrow keys to respectively undo and redo a move.

You can play against the engine or you can start the engine twice (in two separate CMD windows) and make both instances play again each other! For this click on "TO PLAY" on one of the instances.

## Using the GUI-less engine version (name ends with a 'x')

The GUI-less engine does not need any additional file.

Add the path to the program in XBOARD (under linux) or WINBOARD (under Windows) list of engines
Select the engine and play !
