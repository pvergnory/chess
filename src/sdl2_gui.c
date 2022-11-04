#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "common.h"

char* message[7] = {
    "It's your turn",
    "Check !",
    "Check Mat !",
    "You win !",
    "I am Pat",
    "Thinking...",
    "Playing this !"};

int engine_is_black;

//------------------------------------------------------------------------------------
// Communication between 2 instances of the game
//------------------------------------------------------------------------------------

void log_info(const char* str)
{
    fputs(str, stdout);
}

void send_str(const char* str)
{
    fputs(str, stdout);
}

static void init_communications(void)
{
    remove("move.chs");
    remove("white_move.chs");
    remove("black_move.chs");
}

static void transmit_move(char* move)
{
    char* file_name = (play & 1) ? "white_move.chs" : "black_move.chs";
    FILE* f = fopen("move.chs", "w");
    if (f == NULL) return;

    fprintf(f, "%d: %s\n", play - 1, move);
    fflush(f);
    fclose(f);
    rename("move.chs", file_name);
}

static int receive_move(char* move)
{
    char str[40];
    int p;
    char* file_name = (play & 1) ? "black_move.chs" : "white_move.chs";
    struct stat bstat;
    if (stat(file_name, &bstat) != 0) return 0;

    FILE* f = fopen(file_name, "r");
    if (f == NULL) return 0;
    if (fscanf(f, "%d: %s\n", &p, str) < 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    remove(file_name);
    while (stat(file_name, &bstat) == 0) continue;

    if (p != play) {
        printf("Received move %s for play %d but play is %d\n", str, p, play);
        return 0;
    }
    memcpy(move, str, 5);
    return 1;
}

//------------------------------------------------------------------------------------
// Save / Load a game
//------------------------------------------------------------------------------------

static void save_game(void)
{
    FILE* f = fopen("game.chess", "w");
    if (f == NULL) {
        fprintf(stderr, "Cannot open file for writing\n");
        exit(-1);
    }
    for (int p = 0; p < nb_plays; p++) fprintf(f, "%s\n", get_move_str(p));
    fclose(f);
}

static void load_game(void)
{
    FILE* f = fopen("game.chess", "r");
    if (f == NULL) {
        fprintf(stderr, "Cannot open file for reading\n");
        return;
    }

    init_game(NULL);
    char move[8];
    while (1) {
        memset(move, 0, sizeof(move));
        if (fscanf(f, "%[^\n]", move) == EOF) break;
        fgetc(f);  // skip '\n'
        printf("play %d: move %s\n", play, move);
        if (try_move(move) != 1) break;
    }
    nb_plays = play;
    fclose(f);
}

//------------------------------------------------------------------------------------
// Graphical elements
//------------------------------------------------------------------------------------

char* piece_ch = "pknbrqPKNBRQ";

#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480
#define SQUARE_WIDTH  50
#define PIECE_WIDTH   32
#define PIECE_HEIGHT  32
#define MARGIN        20
#define PIECE_MARGIN  (MARGIN + (SQUARE_WIDTH - PIECE_WIDTH) / 2)

SDL_Window* win;
SDL_Texture* tex;
SDL_Renderer* render;
SDL_Rect text_rect;
SDL_Texture* text_texture;
TTF_Font *s_font, *font, *h_font;
int mx, my;  // mouse position

static void graphical_inits(char* name)
{
    // Load the text fonts
    TTF_Init();
    s_font = TTF_OpenFont("resources/FreeSans.ttf", 12);
    if (s_font == NULL) {
        fprintf(stderr, "error: small font not found\n");
        exit(EXIT_FAILURE);
    }
    font = TTF_OpenFont("resources/OptimusPrinceps.ttf", 20);
    if (font == NULL) {
        fprintf(stderr, "error: font not found\n");
        exit(EXIT_FAILURE);
    }
    h_font = TTF_OpenFont("resources/OptimusPrincepsSemiBold.ttf", 20);
    if (font == NULL) {
        fprintf(stderr, "error: bold font not found\n");
        exit(EXIT_FAILURE);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);
    if (!win) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    render = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!render) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // load the chess pieces image
    SDL_Surface* surface = IMG_Load("resources/Chess_Pieces.png");
    if (!surface) {
        fprintf(stderr, "SDL surface error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(render);
        SDL_DestroyWindow(win);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // move it to a texture
    tex = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);
    if (!tex) {
        fprintf(stderr, "SDL texture error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(render);
        SDL_DestroyWindow(win);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Capture also 1st click event than regains the window
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
}

static void graphical_closes(void)
{
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

static void put_text(TTF_Font* f, char* text, int x, int y)
{
    SDL_Surface* surface;
    SDL_Color textColor = {0, 0, 0, 0};

    text_rect.x = x;
    text_rect.y = y;

    surface      = TTF_RenderText_Solid(f, text, textColor);
    text_texture = SDL_CreateTextureFromSurface(render, surface);
    text_rect.w  = surface->w;
    text_rect.h  = surface->h;
    SDL_FreeSurface(surface);

    SDL_RenderCopy(render, text_texture, NULL, &text_rect);

    SDL_DestroyTexture(text_texture);
}

static int put_menu_text(char* text, int x, int y)
{
    int ret = 0;
    SDL_Surface* surface;
    SDL_Color textColor = {0, 0, 0, 0};

    text_rect.x = x;
    text_rect.y = y;

    surface = TTF_RenderText_Solid(font, text, textColor);
    if (mx >= x && mx < x + surface->w && my >= y && my < y + surface->h) {
        SDL_FreeSurface(surface);
        surface = TTF_RenderText_Solid(h_font, text, textColor);
        ret = 1;
    }

    text_texture = SDL_CreateTextureFromSurface(render, surface);
    text_rect.w  = surface->w;
    text_rect.h  = surface->h;
    SDL_FreeSurface(surface);

    SDL_RenderCopy(render, text_texture, NULL, &text_rect);

    SDL_DestroyTexture(text_texture);

    return ret;
}

static void draw_piece(char piece, int x, int y)
{
    if (piece == ' ') return;

    // get piece zone in pieces PNG file
    int p = strchr(piece_ch, piece) - piece_ch;
    SDL_Rect sprite = { p * PIECE_WIDTH, 0, PIECE_WIDTH, PIECE_HEIGHT };

    SDL_Rect dest   = { x, y, PIECE_WIDTH, PIECE_HEIGHT};
    SDL_RenderCopy(render, tex, &sprite, &dest);
}

static int mouse_to_sq64(int x, int y)
{
    int l = 7 - ((y - MARGIN) / SQUARE_WIDTH);
    int c = (x - MARGIN) / SQUARE_WIDTH;
    if (c < 0 || c > 7 || l < 0 || l > 7) return -1;
    return c + 8 * l;
}

static void display_board()
{
    SDL_Rect full_window = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_Rect rect        = {0, 0, 8 * SQUARE_WIDTH + 2 * MARGIN, 8 * SQUARE_WIDTH + 2 * MARGIN};
    char ch;

    // Clear the window
    SDL_RenderClear(render);
    SDL_SetRenderDrawColor(render, 252, 237, 226, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(render, &full_window);

    SDL_SetRenderDrawColor(render, 250, 238, 203, 255);
    SDL_RenderFillRect(render, &rect);

    rect.w = SQUARE_WIDTH;
    rect.h = SQUARE_WIDTH;
    for (int l = 0; l < 8; l++) {
        for (int c = 0; c < 8; c++) {
            rect.x = MARGIN + SQUARE_WIDTH * c;
            rect.y = MARGIN + SQUARE_WIDTH * (7 - l);
            if ((l + c) & 1) SDL_SetRenderDrawColor(render, 245, 225, 164, 255);
            else SDL_SetRenderDrawColor(render, 205, 146, 20, 255);
            SDL_RenderFillRect(render, &rect);

            draw_piece(get_piece(l, c), PIECE_MARGIN + SQUARE_WIDTH * c, PIECE_MARGIN + SQUARE_WIDTH * (7 - l));
        }
        ch = 'a' + l;
        put_text(s_font, &ch, MARGIN + SQUARE_WIDTH / 2 - 3 + l * SQUARE_WIDTH, MARGIN / 2 - 7);
        put_text(s_font, &ch, MARGIN + SQUARE_WIDTH / 2 - 3 + l * SQUARE_WIDTH, MARGIN + 8 * SQUARE_WIDTH);

        ch = '8' - l;
        put_text(s_font, &ch, MARGIN / 2 - 3, MARGIN + SQUARE_WIDTH / 2 - 3 + l * SQUARE_WIDTH);
        put_text(s_font, &ch, MARGIN + 8 * SQUARE_WIDTH + 7, MARGIN + SQUARE_WIDTH / 2 - 3 + l * SQUARE_WIDTH);
    }
}

#define MOUSE_OVER_NEW  1
#define MOUSE_OVER_SAVE 2
#define MOUSE_OVER_LOAD 3
#define MOUSE_OVER_YOU  4
#define MOUSE_OVER_BOOK 5
#define MOUSE_OVER_RAND 6
#define MOUSE_OVER_VERB 7

static int display_all(char piece, int x, int y)
{
    char number_str[10];
    int ret = 0;

    /* Display the board and the pieces that are on it */
    display_board();

    SDL_GetMouseState(&mx, &my);

    /* If a piece is picked by the user with his mouse or is moved by move_animation(), draw it */
    if (piece) {
        if (x && y) draw_piece( piece, x, y );
        else        draw_piece( piece, mx - PIECE_WIDTH/2, my - PIECE_HEIGHT/2 );
    }

    /* Display texts */
    put_text(font, message[game_state], MARGIN, WINDOW_HEIGHT - 30);

    sprintf(number_str, "%d", play);
    put_text(font, number_str, 480, 36);

    draw_piece( (play & 1) ? 'k': 'K', 512, 25 );
    if (put_menu_text("to play", 552, 36)) ret = MOUSE_OVER_YOU;

    if (put_menu_text("New", 480, 87)) ret = MOUSE_OVER_NEW;
    if (put_menu_text("Save", 480, 137)) ret = MOUSE_OVER_SAVE;
    if (put_menu_text("Load", 480, 187)) ret = MOUSE_OVER_LOAD;

    if (put_menu_text(use_book ? "Use book" : "No book ", 480, 287)) ret = MOUSE_OVER_BOOK;
    if (put_menu_text(randomize ? "Random ON" : "Random OFF", 480, 337)) ret = MOUSE_OVER_RAND;
    if (put_menu_text(verbose ? "Verbose" : "No trace", 480, 387)) ret = MOUSE_OVER_VERB;

    SDL_RenderPresent(render);

    return ret;
}

static void move_animation(char* move)
{
    int c0 = move[0] - 'a';
    int l0 = move[1] - '1';
    int x0 = PIECE_MARGIN + SQUARE_WIDTH * c0;
    int y0 = PIECE_MARGIN + SQUARE_WIDTH * (7 - l0);

    int c = move[2] - 'a';
    int l = move[3] - '1';
    int x = PIECE_MARGIN + SQUARE_WIDTH * c;
    int y = PIECE_MARGIN + SQUARE_WIDTH * (7 - l);

    char piece = get_piece(l, c);
    set_piece(' ', l, c);

    for (int i = 1; i < 8; i++) {
        display_all(piece, x0 + i * (x - x0) / 8, y0 + i * (y - y0) / 8);
        SDL_Delay(8);
    }
    set_piece(piece, l, c);
}

//------------------------------------------------------------------------------------
// debug stuff
//------------------------------------------------------------------------------------

int trace = 0;

extern char* board_ptr;
#define B(x)        (*(board_ptr + (x)))
#define Board(l, c) B(10 * (l) + (c))

static void debug_actions(char ch)
{
    if (ch == 't') {
        trace = 1 - trace;
        return;
    }

    int sq64 = mouse_to_sq64(mx, my);
    if (sq64 < 0) return;

    if (ch == 'w') { printf("B(%d) = %d\n", sq64, Board(sq64 / 8, sq64 % 8)); }

    set_piece(ch, sq64 / 8, sq64 % 8);
}

//------------------------------------------------------------------------------------
// Get the user move
//------------------------------------------------------------------------------------

static void get_piece_from(int sq64, char* piece)
{
    if (sq64 < 0) return;

    // The player must pick one of his pieces
    char ch = get_piece(sq64 / 8, sq64 % 8);
    if (ch == ' ') return;
    if ((play & 1) && !(ch & 0x20)) return;
    if (!(play & 1) && (ch & 0x20)) return;
    *piece = ch;
    set_piece(' ', sq64 / 8, sq64 % 8);
}

static int get_move_to(int from64, int to64, char* piece, char* move)
{
    if (to64 < 0 || to64 > 63 || *piece == 0) return 0;

    // put back the piece (it will be moved by try_move() )
    set_piece(*piece, from64 / 8, from64 % 8);
    *piece = 0;

    // Allow the player to put the piece back to its original place (no move)
    if (to64 == from64) return 0;

    move[0] = 'a' + from64 % 8;
    move[1] = '1' + from64 / 8;
    move[2] = 'a' + to64 % 8;
    move[3] = '1' + to64 / 8;
    move[4] = 0;
    return 1;
}

//------------------------------------------------------------------------------------
// Main: program entry, initial setup and then game loop
//------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    (void)argc;

    int mouse_over = 0;
    char piece     = 0;
    int from64 = 0, to64 = 0;
    char move[8];

    char* name;
    if ((name = strrchr(argv[0], '/'))) name++;        // Linux
    else if ((name = strrchr(argv[0], '\\'))) name++;  // Windows
    else name = argv[0];

    init_game(NULL);
    engine_is_black = 1;
    randomize = 1;
    graphical_inits(name);
    init_communications();

    while (1) {
        // Refresh the display
        mouse_over = display_all(piece, 0, 0);

        memset(move, 0, sizeof(move));

        // Wait for an event
        SDL_Event event;
        while (SDL_WaitEventTimeout(&event, 20) == 0) {
            // While waiting for an event, check if a program sent us its move
            if (receive_move(move)) {
                if (try_move(move)) {
                    game_state = ANIM_GS;
                    goto think;
                }
            }
        }

        // Event is 'Quit'
        if (event.type == SDL_QUIT) {
            graphical_closes();
            return 0;
        }

        // Event is a mouse click
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            // handle mouse over a button
            switch (mouse_over) {
            case MOUSE_OVER_NEW:
                init_game(NULL);
                engine_is_black = 1;
                continue;
            case MOUSE_OVER_SAVE:
                mouse_over = display_all(0, 0, 0);
                save_game();
                continue;
            case MOUSE_OVER_LOAD:
                mouse_over = display_all(0, 0, 0);
                load_game();
                continue;
            case MOUSE_OVER_YOU:
                engine_is_black = play & 1;
                init_communications();
                game_state = THINK_GS;
                break;
            case MOUSE_OVER_BOOK:
                use_book = 1 - use_book;
                continue;
            case MOUSE_OVER_RAND:
                randomize = 1 - randomize;
                continue;
            case MOUSE_OVER_VERB:
                verbose = 1 - verbose;
                continue;

            default:  // handle mouse over the board
                from64 = mouse_to_sq64(event.button.x, event.button.y);
                get_piece_from(from64, &piece);
            }
        }
        else if (event.type == SDL_MOUSEBUTTONUP) {
            to64 = mouse_to_sq64(event.button.x, event.button.y);
            if (get_move_to(from64, to64, &piece, move))
                if (try_move(move)) game_state = THINK_GS;
        }

        // Event is a keyboard input
        else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_LEFT) {  // undo
                user_undo_move();
                piece      = 0;
                game_state = WAIT_GS;
                init_communications();
            }
            if (event.key.keysym.sym == SDLK_RIGHT) {  // redo
                user_redo_move();
                piece      = 0;
                game_state = WAIT_GS;
                init_communications();
            }
            if (event.key.keysym.sym <= 'z') {
                char ch = (char)(event.key.keysym.sym);
                if (event.key.keysym.mod & KMOD_SHIFT) ch += ('A' - 'a');
                debug_actions(ch);
            }
        }
think:
        // To the program to play
        if (game_state >= THINK_GS) {
            if (game_state == ANIM_GS) {
                move_animation(move);
                game_state = THINK_GS;
            }
            piece = 0;
            display_all(0, 0, 0);

            compute_next_move();
            if (game_state <= MAT_GS) {
                transmit_move(engine_move_str);
                move_animation(engine_move_str);
            }
        }
    }
    return 0;
}
