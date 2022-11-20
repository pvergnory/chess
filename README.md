# chess and chessx

A small chess engine, written in C, with a graphical user interface (**chess**) or with an interface to XBoard/WinBoard (**chessx**). You can play against it, or you can even let it play against it-self !

## chess features

- 10x10 bytes board representation: the 8x8 board + a 1-square thick border around it
- Entire board copy at each new move, so undoing a move is very simple
- Negamax search with alpha beta pruning
- Iterative deepening
- Move ordering : Principal Variation move, then "killer move", then MVV/LVA attacks, then other moves
- Transposition table (using Pengy hash)
- Evaluation pruning
- Futility prunning
- Opening book

## Pre-requisites to build

Under Windows, msys64 and MINGW64 must be installed.
Under Linux, gcc must be installed

Under the two platforms, the GUI uses the SDL2 graphical library

## Building chess and chessx

To build the two engines, use `build.bat` on Windows or `build` on Linux without any argument.

## Using chess (Linux), or chess.exe (Windows)

The program needs resources related to the GUI, that are grouped in the 'resources' folder.

On Windows, the program needs also to have access to the 4 following graphical DLLs. These DLLs can be in the same directory as the program or in a directory listed in the PATH environment variable:
- SDL2.dll
- SDL2_image.dll
- SDL2_ttf.dll
- libfreetype-6.dll

Use the mouse to move a piece, use the left arrow and right arrow keys to respectively undo and redo a move.

You can play against the engine or you can start the engine twice (in two separate CMD or terminal windows but in the same directory) and make both instances play against each other! For this, click on "TO PLAY" on one of the instances.

## Using GUI-less engine chessx (Linux) or chessx.exe (Windows)

The GUI-less engine does not need any additional file. It is totally standalone.

Start XBOARD under linux (or WINBOARD under Windows). Via the GUI, add the engine and its path in the list of engines (to be done once only). Select the engine and play !