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
@gcc src\bin_to_h.c -o bin_to_h.exe
@bin_to_h resources\OptimusPrinceps.ttf src\font_ttf.h
@bin_to_h resources\Chess_Pieces.svg src\pieces_svg.h
@del bin_to_h.exe
@gcc src/chess.c src/engine.c SDL2_image.dll SDL2_ttf.dll libfreetype-6.dll -o chess.exe -Wall -Wextra -Wpedantic -Wimplicit-fallthrough=0 -lmingw32 -lSDL2main -lSDL2 -O3 -DWITH_BOOK -s
@del src\font_ttf.h
@del src\pieces_svg.h
@echo.
@echo Archive all the files needed to run it
@echo --------------------------------------
@tar -czvf chess.tar.gz chess.exe SDL2.dll SDL2_image.dll SDL2_ttf.dll libfreetype-6.dll
@echo.
@echo Compile the chess engine for XBOARD
@echo -----------------------------------
@gcc src/chessx.c src/engine.c -o chessx.exe -Wall -Wextra -Wimplicit-fallthrough=0 -Wpedantic -lmingw32 -lpthread -O3 -DWITH_BOOK -s
@del src\book.h
@echo.
