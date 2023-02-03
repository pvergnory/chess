#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include "engine.h"

char* message[7] = {
    "It's your turn",
    "Check !",
    "Check Mat !",
    "You win !",
    "I am Pat",
    "Thinking...",
    "Playing this !"};

void log_info(const char* str)
{
    fputs(str, stdout);
}

void send_str(const char* str)
{
    fputs(str, stdout);
}

//------------------------------------------------------------------------------------
// Communication between 2 instances of the game
//------------------------------------------------------------------------------------

static void init_communications(void)
{
    remove("move.chs");
    remove("white_move.chs");
    remove("black_move.chs");
}

static void transmit_move(char* move)
{
    remove("white_move.chs");
    remove("black_move.chs");

    FILE* f = fopen("move.chs", "w");
    if (f == NULL) return;
    fprintf(f, "%d: %s\n", play - 1, move);
    fflush(f);
    fclose(f);

    rename("move.chs", (play & 1) ? "white_move.chs" : "black_move.chs");
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
        return;
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
    char move_str[8];
    while (1) {
        memset(move_str, 0, sizeof(move_str));
        if (fscanf(f, "%[^\n]", move_str) == EOF) break;
        fgetc(f);  // skip '\n'
        printf("play %d: move %s\n", play, move_str);
        if (try_move_str(move_str) != 1) break;
    }
    nb_plays = play;
    fclose(f);
}

//------------------------------------------------------------------------------------
// Graphical elements
//------------------------------------------------------------------------------------

#define SQUARE_W  62
#define PIECE_W   60
#define MARGIN    20
#define PIECE_M   ((SQUARE_W - PIECE_W) / 2)
#define TEXT_X    (3*MARGIN + 8*SQUARE_W + 2*MARGIN)
#define TEXT_Y    (2*MARGIN + SQUARE_W/2 - 6)
#define WINDOW_W  (3*MARGIN + 8*SQUARE_W + TEXT_Y + 120)
#define WINDOW_H  (3*MARGIN + 8*SQUARE_W + 40)

static TTF_Font      *s_font, *m_font, *font, *h_font;
static SDL_Window*   win = NULL;
static SDL_Texture*  tex = NULL;
static SDL_Renderer* render = NULL;
static SDL_Texture*  text_texture = NULL;
static int           mx, my;  // mouse position
static int           side_view = 0;

static void exit_with_message( char* error_msg) {
    fprintf(stderr, "%s\n", error_msg);
    exit(EXIT_FAILURE);
}

static void graphical_exit( char* error_msg)
{
    if (error_msg) fprintf(stderr, "%s: %s\n", error_msg, SDL_GetError());
    if (tex)       SDL_DestroyTexture(tex);
    if (render)    SDL_DestroyRenderer(render);
    if (win)       SDL_DestroyWindow(win);
    SDL_Quit();
    if (error_msg) exit(EXIT_FAILURE);
}

unsigned char font_ttf[] = {
    #include "font_ttf.h"
};

unsigned char pieces_svg[] = {
    #include "pieces_svg.h"
};

static void graphical_inits(char* name)
{
    SDL_RWops* rw_hdl;

    // Load the text fonts
    TTF_Init();

    rw_hdl = SDL_RWFromConstMem( (void*) font_ttf, sizeof(font_ttf) );
    s_font = TTF_OpenFontRW( rw_hdl, 1, 14);
    if (s_font == NULL) exit_with_message( "error: small font not found" );

    rw_hdl = SDL_RWFromConstMem( (void*) font_ttf, sizeof(font_ttf) );
    m_font = TTF_OpenFontRW( rw_hdl, 1, 18);
    if (m_font == NULL) exit_with_message( "error: medium font not found" );

    rw_hdl = SDL_RWFromConstMem( (void*) font_ttf, sizeof(font_ttf) );
    font   = TTF_OpenFontRW( rw_hdl, 1, 20);
    if (font == NULL) exit_with_message( "error: normal font not found" );

    rw_hdl = SDL_RWFromConstMem( (void*) font_ttf, sizeof(font_ttf) );
    h_font = TTF_OpenFontRW( rw_hdl, 1, 22);
    if (h_font == NULL)  exit_with_message( "error: bold font not found" );

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) 
        graphical_exit( "SDL init error" );

    win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, 0);
    if (!win) graphical_exit( "SDL window creation error" );

    render = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!render) graphical_exit( "SDL render creation error");

    // load the chess pieces image
    rw_hdl = SDL_RWFromConstMem( (void*) pieces_svg, sizeof(pieces_svg) );
    SDL_Surface* surface = IMG_LoadTyped_RW( rw_hdl, 1, "svg" );
    if (!surface) graphical_exit( "SDL image load error");

    // move it to a texture
    tex = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);
    if (!tex)  graphical_exit( "SDL texture creation error");

    // Capture also 1st click event than regains the window
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
}

static void put_text(TTF_Font* f, char* text, int x, int y)
{
    SDL_Color textColor = {40, 40, 40, 0};
    SDL_Surface* surface = TTF_RenderText_Blended(f, text, textColor);
    SDL_Rect text_rect = { x, y, surface->w, surface->h };

    text_texture = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);

    SDL_RenderCopy(render, text_texture, NULL, &text_rect);
    SDL_DestroyTexture(text_texture);
}

static int put_menu_text(char* text, int x, int y, int id)
{
    int ret = 0;

    SDL_Color textColor = {40, 40, 40, 0};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, textColor);
    if (mx >= x && mx < x + surface->w && my >= y && my < y + surface->h) {
        SDL_FreeSurface(surface);
        textColor.r = 0, textColor.g = 0, textColor.b = 0;
        surface = TTF_RenderText_Blended(h_font, text, textColor);
        ret = id;
    }
    SDL_Rect text_rect = { x, y, surface->w, surface->h };

    text_texture = SDL_CreateTextureFromSurface(render, surface);
    SDL_FreeSurface(surface);

    SDL_RenderCopy(render, text_texture, NULL, &text_rect);
    SDL_DestroyTexture(text_texture);

    return ret;
}

static void draw_piece(char piece, int x, int y)
{
    if (piece == ' ') return;

    // get piece zone in pieces PNG file
    char* piece_ch = "pknbrqPKNBRQ";
    int p = strchr(piece_ch, piece) - piece_ch;
    SDL_Rect sprite = { p * PIECE_W, 0, PIECE_W, PIECE_W };

    SDL_Rect dest   = { x, y, PIECE_W, PIECE_W};
    SDL_RenderCopy(render, tex, &sprite, &dest);
}

static int mouse_to_sq64(int x, int y)
{
    if (x < 2*MARGIN) return -1;
    if (y < 2*MARGIN) return -1;
	
    int l = (side_view) ? (y - 2*MARGIN)/SQUARE_W : 7 - (y - 2*MARGIN)/SQUARE_W;
    int c = (side_view) ? 7 - (x - 2*MARGIN)/SQUARE_W : (x - 2*MARGIN)/SQUARE_W;
    if (c < 0 || c > 7 || l < 0 || l > 7) return -1;

    return c + 8*l;
}

#define MOUSE_OVER_YOU  1
#define MOUSE_OVER_NEW  2
#define MOUSE_OVER_SAVE 3
#define MOUSE_OVER_LOAD 4
#define MOUSE_OVER_BOOK 5
#define MOUSE_OVER_RAND 6
#define MOUSE_OVER_VERB 7
#define MOUSE_OVER_BRD  8
#define MOUSE_OVER_BB   9

static int display_board( int from64, int show_possible_moves)
{
    SDL_Rect full_window = {0, 0, WINDOW_W, WINDOW_H};
    SDL_Rect rect        = {MARGIN, MARGIN, 8*SQUARE_W + 2*MARGIN, 8*SQUARE_W + 2*MARGIN};
    SDL_Rect mark        = {0, 0, 8, 8};
    char ch;

    // Detect if the mouse is over the board or its border
    int ret = 0;
    if (MARGIN <= mx && mx < 3*MARGIN + 8*SQUARE_W
     && MARGIN <= my && my < 3*MARGIN + 8*SQUARE_W)
        ret = MOUSE_OVER_BB;
    if (2*MARGIN <= mx && mx < 2*MARGIN + 8*SQUARE_W
     && 2*MARGIN <= my && my < 2*MARGIN + 8*SQUARE_W)
        ret = MOUSE_OVER_BRD;
    TTF_Font* f = (ret == MOUSE_OVER_BB) ? m_font : s_font;

    // Clear the window
    SDL_RenderClear(render);
    SDL_SetRenderDrawColor(render, 230, 217, 181, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(render, &full_window);

    SDL_SetRenderDrawColor(render, 250, 238, 203, 255);
    SDL_RenderFillRect(render, &rect);

    rect.w = SQUARE_W;
    rect.h = SQUARE_W;
    for (int l = 0, sq64 = 0; l < 8; l++) {
        for (int c = 0; c < 8; c++, sq64++) {
            rect.x = 2*MARGIN + ((side_view) ? 7 - c : c)*SQUARE_W;
            rect.y = 2*MARGIN + ((side_view) ? l : 7 - l)*SQUARE_W;
            if ((l + c) & 1) SDL_SetRenderDrawColor(render, 230, 217, 181, 255);
            else SDL_SetRenderDrawColor(render, 176, 126, 83, 255);
            SDL_RenderFillRect(render, &rect);

            if (sq64 == from64) continue; // Don't draw the piece being moved
            draw_piece( get_piece(l, c), rect.x + PIECE_M, rect.y + PIECE_M);

            if (show_possible_moves) {
                if (get_possible_moves_board(l, c)) {
                    mark.x = rect.x + SQUARE_W/2 -4;
                    mark.y = rect.y + SQUARE_W/2 -4;
                    if ((l + c) & 1) SDL_SetRenderDrawColor(render, 176, 126, 83, 255);
                    else             SDL_SetRenderDrawColor(render, 230, 217, 181, 255);
                    SDL_RenderFillRect(render, &mark);
                }
            }
        }
        ch = (side_view) ? 'h' - l : 'a' + l;
        put_text(f, &ch, 2*MARGIN + SQUARE_W/2 - 3 + l*SQUARE_W, MARGIN + MARGIN/2 - 7);
        put_text(f, &ch, 2*MARGIN + SQUARE_W/2 - 3 + l*SQUARE_W, 2*MARGIN + 8*SQUARE_W + 2);

        ch = (side_view) ? '1' + l : '8' - l;
        put_text(f, &ch, MARGIN + 8, 2*MARGIN + SQUARE_W/2 - 7 + l*SQUARE_W);
        put_text(f, &ch, 2*MARGIN + 8*SQUARE_W + 8, 2*MARGIN + SQUARE_W/2 - 7 + l*SQUARE_W);
    }
    return ret;
}

static int display_all(int from64, int x, int y)
{
    char play_str[20];

    SDL_GetMouseState(&mx, &my);

    /* Display the board and the pieces that are on it */
    int ret = display_board( from64, (from64 >= 0 && !x && !y) );

    /* If a piece is picked by the user or is moved by move_animation(), draw it */
    if (from64 >= 0) {
        char piece = get_piece( from64 / 8, from64 % 8);
        if (x || y) draw_piece( piece, x, y );
        else        draw_piece( piece, mx - PIECE_W/2, my - PIECE_W/2 );
    }

    /* Display turn and play iteration */
    draw_piece( (play & 1) ? 'k': 'K', TEXT_X, 2*MARGIN + PIECE_M );
    if (game_state == THINK_GS) {
        sprintf(play_str, "Playing %d ...", play);
        put_text(font, play_str, TEXT_X, TEXT_Y + SQUARE_W);
    }
    else {
        sprintf(play_str, "Play %d", play);
        ret += put_menu_text(play_str, TEXT_X, TEXT_Y + SQUARE_W, MOUSE_OVER_YOU);
    }

    /* Display other buttons and texts */
    ret += put_menu_text("New", TEXT_X, TEXT_Y + 2*SQUARE_W, MOUSE_OVER_NEW);
    ret += put_menu_text("Save", TEXT_X, TEXT_Y + 3*SQUARE_W, MOUSE_OVER_SAVE);
    ret += put_menu_text("Load", TEXT_X, TEXT_Y + 4*SQUARE_W, MOUSE_OVER_LOAD);
    ret += put_menu_text(use_book ? "Use book" : "No book ", TEXT_X, TEXT_Y + 5*SQUARE_W, MOUSE_OVER_BOOK);
    ret += put_menu_text(randomize ? "Random" : "Ordered", TEXT_X, TEXT_Y + 6*SQUARE_W, MOUSE_OVER_RAND);
    ret += put_menu_text(verbose ? "Verbose" : "No trace", TEXT_X, TEXT_Y + 7*SQUARE_W, MOUSE_OVER_VERB);
    put_text(font, message[game_state], MARGIN, WINDOW_H - 32);

    SDL_RenderPresent(render);
    return ret;
}

static void move_animation(char* move)
{
    int c0 = move[0] - 'a';
    int l0 = move[1] - '1';
    int x0 = 2*MARGIN + ((side_view) ? 7 - c0 : c0)*SQUARE_W + PIECE_M;
    int y0 = 2*MARGIN + ((side_view) ? l0 : 7 - l0)*SQUARE_W + PIECE_M -2; // -2 for a "lift" effect :)

    int c = move[2] - 'a';
    int l = move[3] - '1';
    int dx = ((side_view) ? c0 - c : c - c0)*SQUARE_W;
    int dy = ((side_view) ? l - l0 : l0 - l)*SQUARE_W;

    user_undo_move();
    for (int i = 1; i < 12; i++) {
        display_all( 8*l0 + c0, x0 + (i*dx)/12, y0 + (i*dy)/12);
        SDL_Delay(5);
    }
    user_redo_move();
    display_all(-1, 0, 0);
}

//------------------------------------------------------------------------------------
// debug stuff
//------------------------------------------------------------------------------------

int trace = 0;

static void debug_actions(char ch)
{
    if (ch == 't') {
        trace = 1 - trace;
        return;
    }

    int sq64 = mouse_to_sq64(mx, my);
    if (sq64 < 0) return;
    set_piece(ch, sq64 / 8, sq64 % 8);
}

//------------------------------------------------------------------------------------
// Handle external actions (user, other program)
//------------------------------------------------------------------------------------

static int check_from(int sq64)
{
    if (sq64 < 0) return -1;

    // The player must pick one of his pieces
    char ch = get_piece(sq64 / 8, sq64 % 8);
    if (ch == ' ') return -1;
    if ((play & 1) && !(ch & 0x20)) return -1;
    if (!(play & 1) && (ch & 0x20)) return -1;

    set_possible_moves_board(sq64 / 8, sq64 % 8);
    return sq64;
}

static int get_move_to(int from64, int to64, char* move_str)
{
    // Allow the player to put the piece back to its original place (no move)
    if (to64 < 0 || to64 == from64) return 0;

    move_str[0] = 'a' + from64 % 8;
    move_str[1] = '1' + from64 / 8;
    move_str[2] = 'a' + to64 % 8;
    move_str[3] = '1' + to64 / 8;
    move_str[4] = 0;
    return 1;
}

static int handle_user_turn( char* move_str)
{
    int from64 = -1;  // -1 = "no piece currently picked by the user"
    int mouse_over;

    while (1) {
        // Refresh the display
        mouse_over = display_all( from64, 0, 0);

        // Check if a program sent us its move
        if (receive_move(move_str))
            if (try_move_str(move_str)) return ANIM_GS;

        SDL_Delay(10);

        // Handle Mouse and keyboard events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Event is 'Quit'
            if (event.type == SDL_QUIT) return QUIT_GS;

            // Event is a mouse click
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                // handle mouse over a button
                switch (mouse_over) {
                case MOUSE_OVER_YOU:  return THINK_GS;
                case MOUSE_OVER_NEW:  init_game(NULL); break;
                case MOUSE_OVER_SAVE: save_game(); break;
                case MOUSE_OVER_LOAD: load_game(); break;
                case MOUSE_OVER_BOOK: use_book = !use_book; break;
                case MOUSE_OVER_RAND: randomize = !randomize; break;
                case MOUSE_OVER_VERB: verbose = !verbose; break;
                case MOUSE_OVER_BB:   side_view = !side_view; break;
                case MOUSE_OVER_BRD:  from64 = check_from(mouse_to_sq64(event.button.x, event.button.y));
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP && from64 >= 0) {
                int to64 = mouse_to_sq64(event.button.x, event.button.y);
                if (get_move_to(from64, to64, move_str)) {
                    if (try_move_str(move_str)) {
                        display_all(-1, 0, 0);
                        return THINK_GS;
                    }
                }
                from64 = -1;
            }

            // Event is a keyboard input
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_LEFT) {        // undo
                    user_undo_move();
                    init_communications();
                }
                else if (event.key.keysym.sym == SDLK_RIGHT) {  // redo
                    user_redo_move();
                }
                else if (event.key.keysym.sym <= 'z') {         // debug
                    char ch = (char)(event.key.keysym.sym);
                    if (event.key.keysym.mod & KMOD_SHIFT) ch += ('A' - 'a');
                    debug_actions(ch);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------
// Main: program entry, initial setup and then game loop
//------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    (void)argc;
    char move_str[8];

    // A few inits
    char* name;
    if ((name = strrchr(argv[0], '/'))) name++;        // Linux
    else if ((name = strrchr(argv[0], '\\'))) name++;  // Windows
    else name = argv[0];
    graphical_inits(name);
    init_game(NULL);
    randomize = 1;
    init_communications();

    // The game loop
    while (1) {
        // To the user to play
        game_state = handle_user_turn(move_str);
        if (game_state == QUIT_GS) break;
        if (game_state == ANIM_GS) {
            move_animation(move_str);
            game_state = THINK_GS;
        }
        // To the program to play
        compute_next_move();
        if (game_state <= MAT_GS) {
            transmit_move(engine_move_str);
            move_animation(engine_move_str);
        }
    }
    init_communications();
    graphical_exit(NULL);
    return 0;
}
