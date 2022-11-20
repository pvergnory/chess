@echo Make the book
@echo -------------
@cd src
@gcc mk_book.c -o mk_book.exe
@mk_book openings.txt
@del mk_book.exe
@cd ..
@echo.
@echo Compile the chess engine with an SDL2-based GUI
@echo -----------------------------------------------
@gcc -g src/chess.c src/sdl2_gui.c SDL2_image.dll SDL2_ttf.dll libfreetype-6.dll -o chess.exe -Wall -Wextra -Wpedantic -Wimplicit-fallthrough=0 -lmingw32 -lSDL2main -lSDL2 -O3
@echo.
@echo Archive all the files needed to run it
@echo --------------------------------------
@tar -czvf chess.tar.gz chess.exe SDL2.dll SDL2_image.dll SDL2_ttf.dll libfreetype-6.dll resources
@echo.
@echo Compile the chess engine for XBOARD
@echo -----------------------------------
@gcc src/chess.c src/xb_if.c -o chessx.exe -Wall -Wextra -Wimplicit-fallthrough=0 -Wpedantic -lmingw32 -lpthread -O3

