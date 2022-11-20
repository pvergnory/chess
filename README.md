# chess and chessx

## chess features

- Engine + Graphical User Interface (chess) and
- Engine + XBOARD/WINBOARD interface (chessx)
- 10x10 bytes board representation
- Entire board copy at each new move, so move undo is very simple
- Negamax search with alpha beta pruning
- Iterative deepening
- Move ordering : Principle Variation move, killer move, MVV/LVA attacks, other moves
- Transposition table (using Pengy hash)
- Evaluation pruning
- Futility prunning
- Opening book

## Pre-requisites to build

Under Windows, msys64 and MINGW64 must be installed.
Under Linux, gcc must be installed

The GUI uses the SDL2 graphical library

## Building chess and chessx

To build the two engines, use build.bat on Windows or build on Linux without any argument.

On Windows: 
build

On Linux: 
./build

## Using the engine version that includes the GUI (chess under linux, chess.exe under Windows)

The executable needs resources grouped in the 'resources' folder.

On Windows, the executable needs also to have access to the 4 following DLLs. These DLLs can be in the same directory as the program or in a directory listed in the PATH environment variable :
- SDL2.dll
- SDL2_image.dll
- SDL2_ttf.dll
- libfreetype-6.dll

Use the mouse to move a piece, use the left arrow and right arrow keys to respectively undo and redo a move.

You can play against the engine or you can start the engine twice (in two separate CMD or terminal windows but in the same directory) and make both instances play against each other! For this, click on "TO PLAY" on one of the instances.

## Using the GUI-less engine version (chessx under linux, chessx.exe under Windows)

Appart from the resources grouped in the 'resources' folder, the GUI-less engine does not need any additional file.

Start XBOARD under linux (or WINBOARD under Windows). Via the GUI, add the engine and its path in the list of engines (to be done once only). Select the engine and play !

