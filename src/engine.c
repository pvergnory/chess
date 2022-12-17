#include <sys/time.h>
#include <x86intrin.h>  // for __rdtsc()

#include "engine.h"

// Chess pieces
enum piece_t {
    TYPE     = 7,
    PAWN     = 2,
    KING     = 3,
    KNIGHT   = 4,
    BISHOP   = 5,
    ROOK     = 6,
    QUEEN    = 7,
    WHITE    = 8,
    W_PAWN   = 10,
    W_KING   = 11,
    W_KNIGHT = 12,
    W_BISHOP = 13,
    W_ROOK   = 14,
    W_QUEEN  = 15,
    BLACK    = 16,
    B_PAWN   = 18,
    B_KING   = 19,
    B_KNIGHT = 20,
    B_BISHOP = 21,
    B_ROOK   = 22,
    B_QUEEN  = 23,
    COLORS   = 24,
    STOP     = 32
};

// A chess board is the 64 squares playable area + a 2-squares thick border around it.
#define BOARD_AND_BORDER_SIZE 100
#define BOARD_SIZE            80  // Smallest contiguous area we need to copy
#define FIRST_BOARDER_SIZE    32  // 32 instead of 21 or 22 for mem aligment
#define NO_POSITION           79  // an impossible position
#define MAX_TURNS             500

static char boards[FIRST_BOARDER_SIZE + BOARD_AND_BORDER_SIZE * MAX_TURNS] __attribute__((aligned(16)));

#define BOARD0 &boards[FIRST_BOARDER_SIZE]
char *board_ptr = BOARD0;
#define B(x)        (*(board_ptr + (x)))
#define Board(l, c) B(10 * (l) + (c))

// Move structure and move tables
#define PROMOTE    1
#define EN_PASSANT 2
#define L_ROOK     3
#define R_ROOK     4
#define BR_CASTLE  5
#define BL_CASTLE  6
#define B_PAWN2    7
#define WR_CASTLE  8
#define WL_CASTLE  9
#define W_PAWN2    10

typedef struct {
    union {
        unsigned int val;
        struct {
            unsigned char from;
            unsigned char to;
            unsigned char eaten;
            unsigned char special;
        };
    };
} move_t;

static move_t best_sequence[LEVEL_MAX+1], best_move[LEVEL_MAX+1], next_best[LEVEL_MAX+1];

// Transposition table to stores move choices for each encountered board situations
#define NEW_BOARD   0
#define OTHER_DEPTH 1
#define UPPER_BOUND 2
#define LOWER_BOUND 3
#define EXACT_VALUE 4

typedef struct {
    union {
        uint64_t hash;      // Pengy hash. Useless LSB taken for depth & flag => 16B table entry size
        struct {            // /!\ struct for little endian CPU !
            uint8_t  depth;
            uint8_t  flag;
            uint16_t dummy[3];
        };
    };
    move_t   move;
    int32_t  eval;
} table_t;

#define TABLE_ENTRIES (1 << 23) // 8 Mega entries.x 16B = 128 MB memory
static table_t table[TABLE_ENTRIES] __attribute__((aligned(16)));

// Move choosen by the chess engine
char *engine_move_str;

int game_state = WAIT_GS;

int play            = 0;
int nb_plays        = 0;
int verbose         = 1;
int use_book        = 1;
int randomize       = 0;
int level_max_max   = LEVEL_MAX;
long time_budget_ms = 2000;
static int mv50     = 0;
static char engine_side;

// The first moves we accept to play
static const move_t first_ply[6] = {
    {.from = 12, .to = 32},
    {.from = 13, .to = 33},
    {.from = 14, .to = 34},
    {.from = 15, .to = 35},
    { .from = 1, .to = 22},
    { .from = 6, .to = 25} };

static move_t *move_ptr;

// Track of the situation at all turns
static move_t moved[MAX_TURNS];
static int board_val[MAX_TURNS];
static int nb_pieces[MAX_TURNS];

// Possible place to eat "en passant" a pawn that moved two rows.
static char en_passant[MAX_TURNS];

// Castle rules: involved rook and king must not have moved before the castle
#define LEFT_CASTLE  1
#define RIGHT_CASTLE 2
#define ALL_CASTLES  3
static char castles[MAX_TURNS + 2];  // (even index: white castle, odd index: black castle)

// Keep track of kings positions
static int king_pos[MAX_TURNS + 2];  // (even index: white king, odd index: black king)

//                     -   P,   P,    K,   N,   B,   R,   Q
static const int piece_value[33] = {0, 100, 100, 4000, 300, 314, 500, 900,
                       0, -100, -100, -4000, -300, -314, -500, -900,
                       0, 100, 100, 4000, 300, 314, 500, 900,
                       0, 0, 0, 0, 0, 0, 0, 0, 0};

// During the first plays, try to occupy the center...
static const int black_pos_bonus[BOARD_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0, 0, 0,
    0, 0, 4, 15, 15, 4, 0, 0, 0, 0,
    0, 0, 10, 8, 8, 10, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, -5, -2, 0, 0, 0, 0, 0, 0, 0};

static const int white_pos_bonus[BOARD_SIZE] = {
    0, -5, -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 10, 8, 8, 10, 0, 0, 0, 0,
    0, 0, 4, 15, 15, 4, 0, 0, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// At the end of the game, the king must avoid the corners
static const int king_pos_malus[BOARD_SIZE] = {
    14, 12, 10, 8, 8, 10, 12, 14, 0, 0,
    12, 9, 7, 6, 6, 7, 9, 12, 0, 0,
    10, 7, 4, 2, 2, 4, 7, 10, 0, 0,
    8, 6, 2, 0, 0, 2, 6, 8, 0, 0,
    8, 6, 2, 0, 0, 2, 6, 8, 0, 0,
    10, 7, 4, 2, 2, 4, 7, 10, 0, 0,
    12, 9, 7, 6, 6, 7, 9, 12, 0, 0,
    14, 12, 10, 8, 8, 10, 12, 14, 0, 0};

// Opening moves book
#include "book.h"

//------------------------------------------------------------------------------------
// Misc conversion functions
//------------------------------------------------------------------------------------

static int str_to_move(char *str, move_t *m)
{
    m->val = 0;

    if (strlen(str) < 4) return 0;

    if (str[0] < 'a' || str[0] > 'h' || str[1] < '0' || str[1] > '8' ||
        str[2] < 'a' || str[2] > 'h' || str[3] < '0' || str[3] > '8') return 0;

    m->from  = str[0] - 'a' + (str[1] - '1') * 10;
    m->to    = str[2] - 'a' + (str[3] - '1') * 10;
    m->eaten = B(m->to);

    // Rebuild 'special'
    char type = B(m->from) & TYPE;
    if (type == KING) {
        if (m->from == 4 && m->to == 6) m->special = WR_CASTLE;
        else if (m->from == 4 && m->to == 2) m->special = WL_CASTLE;
        else if (m->from == 74 && m->to == 76) m->special = BR_CASTLE;
        else if (m->from == 74 && m->to == 72) m->special = BL_CASTLE;
    }
    else if (type <= PAWN) {
        if (m->to <= 7 || m->to >= 70) m->special = PROMOTE;
        else if (m->to - m->from == 20) m->special = W_PAWN2;
        else if (m->from - m->to == 20) m->special = B_PAWN2;
        else if (m->eaten == 0 && (m->to % 10) != (m->from % 10)) m->special = EN_PASSANT;
    }
    else if (type == ROOK) {
        if (m->from == 0 || m->from == 70) m->special = L_ROOK;
        else if (m->from == 7 || m->from == 77) m->special = R_ROOK;
    }
    return 1;
}

static char mv_str[8];
static char *move_str(move_t m)
{
    memset(mv_str, 0, sizeof(mv_str));
    if (m.val == 0) return "";
    sprintf(mv_str, "%c%c%c%c", 'a' + m.from % 10, '1' + m.from / 10,
            'a' + m.to % 10, '1' + m.to / 10);
    if (m.special == PROMOTE) sprintf(mv_str + 4, "q");
    return mv_str;
}

static char piece_char[33] = " .........PKNBRQ..pknbrq.........";

void set_piece(char ch, int l, int c)
{
    char *ptr = strchr(piece_char, ch);
    if (ptr == NULL) return;
    char piece = ptr - piece_char;

    int sq = 10 * l + c;

    if ((B(sq) & COLORS) == 0) nb_pieces[play]++;

    board_val[play] -= piece_value[(int)B(sq)];
    B(sq) = piece;
    board_val[play] += piece_value[(int)B(sq)];

    if (ch == 'k') king_pos[play | 1] = 10 * l + c;
    if (ch == 'K') king_pos[(play + 1) & ~1] = 10 * l + c;
}

char get_piece(int l, int c)
{
    return piece_char[(int)(B(10 * l + c))];
}

char *get_move_str(int p)
{
    return move_str(moved[p]);
}

static struct timeval tv0, tv1;
static long total_ms = 0;

#define start_chrono() gettimeofday(&tv0, NULL);

static long get_chrono(void)
{
    long s, us;
    gettimeofday(&tv1, NULL);
    s  = tv1.tv_sec - tv0.tv_sec;
    us = tv1.tv_usec - tv0.tv_usec;
    return 1000 * s + us / 1000;
}

//------------------------------------------------------------------------------------
// Pengy hash used for the openings book and transposition table
//------------------------------------------------------------------------------------

static uint64_t pengyhash(const void *p, size_t size, uint32_t seed)
{
    uint64_t b[4] = {0};
    uint64_t s[4] = {0, 0, 0, size};
    int i;

    for (; size >= 32; size -= 32, p = (const char *)p + 32) {
        memcpy(b, p, 32);

        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50);
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    memcpy(b, p, size);

    for (i = 0; i < 6; i++) {
        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50) + seed;
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    return s[0] + s[1] + s[2] + s[3];
}

//------------------------------------------------------------------------------------
// Set a board using a FEN string
//------------------------------------------------------------------------------------

static void FEN_to_board(char *str)
{
    int line = 7, col = 0, fm, wc = 0, bc = 0, ep = 0;

    // skip the board occupancy
    char ch, color;
    char *str0 = str;
    while (*str++ != ' ') continue;

    // get the playing side
    color = *str++;
    str++;

    // get the castling states
    while ((ch = *str++) != ' ') {
        if (ch == 'K') wc |= RIGHT_CASTLE;
        else if (ch == 'Q') wc |= LEFT_CASTLE;
        else if (ch == 'k') bc |= RIGHT_CASTLE;
        else if (ch == 'q') bc |= LEFT_CASTLE;
    }

    // get the "en passant" location
    ch = *str++;
    if (ch >= 'a' && ch <= 'h') {
        ep = ch - 'a';
        ch = *str++;
        ep += 10 * (ch - '0');
    }
    else {
        ep = NO_POSITION;
        str++;
    }

    // get the "50 sterile moves" counter
    for (mv50 = 0; (ch = *str++) != ' '; mv50 = mv50 * 10 + ch - '0') continue;

    // get the "full moves" counter and deduce the ply
    for (fm = 0; (ch = *str++) > '0'; fm = fm * 10 + ch - '0') continue;
    play      = 2 * (fm - 1) + (color == 'w') ? 0 : 1;
    nb_plays  = play;
    board_ptr = BOARD0 + BOARD_AND_BORDER_SIZE * play;

    // Now that we know the ply, empty the board ...
    for (line = 0; line < 8; line++)
        for (col = 0; col < 8; col++) set_piece(' ', line, col);

    // ... and fill it with the board occupancy information
    line = 7;
    col  = 0;
    while ((ch = *str0++) != ' ') {
        if (ch == '/') line--, col = 0;
        else if (ch >= '0' && ch <= '9') col += ch - '0';
        else set_piece(ch, line, col++);
    }

    // Set castles and en passant
    castles[play]     = (play & 1) ? bc : wc;
    castles[play + 1] = (play & 1) ? wc : bc;
    en_passant[play]  = ep;
}

//------------------------------------------------------------------------------------
// Game init
//------------------------------------------------------------------------------------

void init_game(char *FEN_string)
{
    memset(boards, STOP, sizeof(boards));  // Set the boards to all borders
    memset(table, 0, sizeof(table));       // Reset the transposition table

    if (FEN_string) FEN_to_board(FEN_string);
    else FEN_to_board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    game_state     = WAIT_GS;
    time_budget_ms = 2000;
    total_ms       = 0;
    randomize      = 0;
    level_max_max  = LEVEL_MAX;
}

//------------------------------------------------------------------------------------
// Make the move, undo it, redo it
//------------------------------------------------------------------------------------

static void do_move(move_t m)
{
    // Remember the whole previous game to be able to undo the move
    memcpy(board_ptr + BOARD_AND_BORDER_SIZE, board_ptr, BOARD_SIZE);
    board_ptr += BOARD_AND_BORDER_SIZE;

    int piece = B(m.from);

    // if it is a king move, keep track of its position and forbid future castles
    king_pos[play + 2] = ((piece & TYPE) == KING) ? m.to : king_pos[play];
    castles[play + 2]  = ((piece & TYPE) == KING) ? 0 : castles[play];

    moved[play] = m;
    play++;
    en_passant[play] = NO_POSITION;

    B(m.from) = 0;
    B(m.to)   = piece;

    board_val[play] = board_val[play - 1];
    nb_pieces[play] = nb_pieces[play - 1];
    if (m.eaten) {
        board_val[play] -= piece_value[m.eaten];
        nb_pieces[play]--;
    }

    switch (m.special) {
    case WR_CASTLE:
        B(7) = 0;
        B(5) = W_ROOK;
        break;
    case WL_CASTLE:
        B(0) = 0;
        B(3) = W_ROOK;
        break;
    case BR_CASTLE:
        B(77) = 0;
        B(75) = B_ROOK;
        break;
    case BL_CASTLE:
        B(70) = 0;
        B(73) = B_ROOK;
        break;
    case PROMOTE:
        B(m.to) |= QUEEN;
        board_val[play] -= piece_value[piece];
        board_val[play] += piece_value[piece | QUEEN];
        break;
    case W_PAWN2:
        en_passant[play] = m.from + 10;  // notice "en passant" possibility
        break;
    case B_PAWN2:
        en_passant[play] = m.from - 10;  // notice "en passant" possibility
        break;
    case EN_PASSANT:  // eat "en passant"
        if (piece == W_PAWN) B(m.to - 10) = 0;
        else                 B(m.to + 10) = 0;
        board_val[play] += piece_value[piece];  // pawn eaten by pawn of opposite color, so...
        nb_pieces[play]--;
        break;
    case L_ROOK:
        castles[play + 1] &= ~LEFT_CASTLE;
        break;
    case R_ROOK:
        castles[play + 1] &= ~RIGHT_CASTLE;
        break;
    }
}

static inline void undo_move(void)
{
    play--;
    board_ptr -= BOARD_AND_BORDER_SIZE;
}

void user_undo_move(void)
{
    if (play) undo_move();
}

void user_redo_move(void)
{
    if (play >= nb_plays) return;
    play++;
    board_ptr += BOARD_AND_BORDER_SIZE;
}

//------------------------------------------------------------------------------------
// Test if the king is in check
//------------------------------------------------------------------------------------

static const int qb_inc[4] = {9, 11, -9, -11};
static const int qr_inc[4] = {10, 1, -10, -1};

static int list_king_protectors(int side, int *king_protectors)
{
    int other = side ^ COLORS;
    int i, inc, p, k_pos = king_pos[play];
    char type;

    // The king does not protect him-self, but like the king protectors,
    // if it moves, its check state must be completely re-evaluated
    king_protectors[0]     = k_pos;
    int king_protectors_nb = 1;

    // test queen and bishop
    for (i = 0; i < 4; i++) {
        inc = qb_inc[i];
        for (p = k_pos + inc; B(p) == 0; p += inc) continue;
        if ((B(p) & COLORS) == side) {
            king_protectors[king_protectors_nb] = p;
            for (p += inc; B(p) == 0; p += inc) continue;
            type = B(p) ^ other;
            if (type == QUEEN || type == BISHOP) king_protectors_nb++;
        }
    }

    // test queen and rook
    for (i = 0; i < 4; i++) {
        inc = qr_inc[i];
        for (p = k_pos + inc; B(p) == 0; p += inc) continue;
        if ((B(p) & COLORS) == side) {
            king_protectors[king_protectors_nb] = p;
            for (p += inc; B(p) == 0; p += inc) continue;
            type = B(p) ^ other;
            if (type == QUEEN || type == ROOK) king_protectors_nb++;
        }
    }
    return king_protectors_nb;
}

static int in_check(int side, int pos)
{
    char type;
    int p;
    int fwd   = (side == WHITE) ? 10 : -10;
    int other = COLORS ^ side;

    // test pawn and king closeby
    if ((B(pos + fwd - 1) ^ other) <= KING) return 1;
    if ((B(pos + fwd + 1) ^ other) <= KING) return 1;

    // king closeby
    if ((B(pos - fwd + 1) ^ other) == KING) return 1;
    if ((B(pos - fwd - 1) ^ other) == KING) return 1;
    if ((B(pos + 1) ^ other) == KING) return 1;
    if ((B(pos - 1) ^ other) == KING) return 1;
    if ((B(pos + 10) ^ other) == KING) return 1;
    if ((B(pos - 10) ^ other) == KING) return 1;

    // test queen and bishop
    for (p = pos + 11; B(p) == 0; p += 11) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) return 1;

    for (p = pos - 11; B(p) == 0; p -= 11) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) return 1;

    for (p = pos + 9; B(p) == 0; p += 9) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) return 1;

    for (p = pos - 9; B(p) == 0; p -= 9) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) return 1;

    // test queen and rook
    for (p = pos + 1; B(p) == 0; p++) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) return 1;

    for (p = pos - 1; B(p) == 0; p--) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) return 1;

    for (p = pos + 10; B(p) == 0; p += 10) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) return 1;

    for (p = pos - 10; B(p) == 0; p -= 10) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) return 1;

    // test knight
    if ((B(pos + 21) ^ other) == KNIGHT) return 1;
    if ((B(pos - 21) ^ other) == KNIGHT) return 1;
    if ((B(pos + 19) ^ other) == KNIGHT) return 1;
    if ((B(pos - 19) ^ other) == KNIGHT) return 1;
    if ((B(pos + 12) ^ other) == KNIGHT) return 1;
    if ((B(pos - 12) ^ other) == KNIGHT) return 1;
    if ((B(pos + 8) ^ other) == KNIGHT) return 1;
    if ((B(pos - 8) ^ other) == KNIGHT) return 1;

    return 0;
}

//------------------------------------------------------------------------------------
// List possible moves
//------------------------------------------------------------------------------------

static void add_move(int from, int to, int special)
{
    move_ptr->from    = from;
    move_ptr->to      = to;
    move_ptr->eaten   = B(to);
    move_ptr->special = special;
    move_ptr++;
}

static int check_move(char blocking, int from, int to)
{
    if (B(to) & blocking) return 0;
    add_move(from, to, 0);
    return (B(to) == 0);
}

static void check_crawler_move(char blocking, int from, int to)
{
    if ((B(to) & blocking) == 0) add_move(from, to, 0);
}

static int check_rook_move(char blocking, int from, int to)
{
    int special = 0;
    if (from == 0 || from == 70) special = L_ROOK;
    if (from == 7 || from == 77) special = R_ROOK;

    if (B(to) & blocking) return 0;
    add_move(from, to, special);
    return (B(to) == 0);
}

static int check_pawn_move(int from, int to, int special)
{
    if (B(to)) return 0;
    if (to < 8 || to >= 70) special = PROMOTE;
    add_move(from, to, special);
    return 1;
}

static void check_wpawn_eat(int from, int to)
{
    int special = 0;
    if (B(to) & BLACK) {
        if (to >= 70) special = PROMOTE;
        add_move(from, to, special);
    }
    else if (en_passant[play] == to)
        add_move(from, to, EN_PASSANT);
}

static void check_bpawn_eat(int from, int to)
{
    int special = 0;
    if (B(to) & WHITE) {
        if (to < 8) special = PROMOTE;
        add_move(from, to, special);
    }
    else if (en_passant[play] == to)
        add_move(from, to, EN_PASSANT);
}

static void list_moves(int pos)
{
    int i;
    char piece    = B(pos);
    char blocking = (B(pos) & COLORS) + STOP;

    switch (piece) {
    case W_KING:
        check_crawler_move(WHITE + STOP, pos, pos - 11);
        check_crawler_move(WHITE + STOP, pos, pos - 10);
        check_crawler_move(WHITE + STOP, pos, pos - 9);
        check_crawler_move(WHITE + STOP, pos, pos + 9);
        check_crawler_move(WHITE + STOP, pos, pos + 10);
        check_crawler_move(WHITE + STOP, pos, pos + 11);
        check_crawler_move(WHITE + STOP, pos, pos - 1);
        check_crawler_move(WHITE + STOP, pos, pos + 1);

        // white castles
        if (pos == 4) {
            if (B(5) == 0 && B(6) == 0 && B(7) == W_ROOK && (castles[play] & RIGHT_CASTLE) && !in_check(WHITE, 4) && !in_check(WHITE, 5) && !in_check(WHITE, 6))
                add_move(pos, 6, WR_CASTLE);
            if (B(3) == 0 && B(2) == 0 && B(1) == 0 && B(0) == W_ROOK && (castles[play] & LEFT_CASTLE) && !in_check(WHITE, 4) && !in_check(WHITE, 3) && !in_check(WHITE, 2))
                add_move(pos, 2, WL_CASTLE);
        }
        break;
    case B_KING:
        check_crawler_move(BLACK + STOP, pos, pos - 11);
        check_crawler_move(BLACK + STOP, pos, pos - 10);
        check_crawler_move(BLACK + STOP, pos, pos - 9);
        check_crawler_move(BLACK + STOP, pos, pos + 9);
        check_crawler_move(BLACK + STOP, pos, pos + 10);
        check_crawler_move(BLACK + STOP, pos, pos + 11);
        check_crawler_move(BLACK + STOP, pos, pos - 1);
        check_crawler_move(BLACK + STOP, pos, pos + 1);

        // black castles
        if (pos == 74) {
            if (B(75) == 0 && B(76) == 0 && B(77) == B_ROOK && (castles[play] & RIGHT_CASTLE) && !in_check(BLACK, 74) && !in_check(BLACK, 75) && !in_check(BLACK, 76))
                add_move(pos, 76, BR_CASTLE);
            if (B(73) == 0 && B(72) == 0 && B(71) == 0 && B(70) == B_ROOK && (castles[play] & LEFT_CASTLE) && !in_check(BLACK, 74) && !in_check(BLACK, 73) && !in_check(BLACK, 72))
                add_move(pos, 72, BL_CASTLE);
        }
        break;
    case W_QUEEN:
    case B_QUEEN:
        for (i = 1; check_move(blocking, pos, pos - i); i++) continue;
        for (i = 1; check_move(blocking, pos, pos + i); i++) continue;
        for (i = 10; check_move(blocking, pos, pos - i); i += 10) continue;
        for (i = 10; check_move(blocking, pos, pos + i); i += 10) continue;
    case W_BISHOP:
    case B_BISHOP:
        for (i = 9; check_move(blocking, pos, pos - i); i += 9) continue;
        for (i = 9; check_move(blocking, pos, pos + i); i += 9) continue;
        for (i = 11; check_move(blocking, pos, pos - i); i += 11) continue;
        for (i = 11; check_move(blocking, pos, pos + i); i += 11) continue;
        break;
    case W_KNIGHT:
    case B_KNIGHT:
        check_crawler_move(blocking, pos, pos + 12);
        check_crawler_move(blocking, pos, pos + 8);
        check_crawler_move(blocking, pos, pos - 12);
        check_crawler_move(blocking, pos, pos - 8);
        check_crawler_move(blocking, pos, pos + 21);
        check_crawler_move(blocking, pos, pos + 19);
        check_crawler_move(blocking, pos, pos - 21);
        check_crawler_move(blocking, pos, pos - 19);
        break;

    case W_ROOK:
    case B_ROOK:
        for (i = 1; check_rook_move(blocking, pos, pos - i); i++) continue;
        for (i = 1; check_rook_move(blocking, pos, pos + i); i++) continue;
        for (i = 10; check_rook_move(blocking, pos, pos - i); i += 10) continue;
        for (i = 10; check_rook_move(blocking, pos, pos + i); i += 10) continue;
        break;

    case W_PAWN:
        if (check_pawn_move(pos, pos + 10, 0) && pos < 20)
            check_pawn_move(pos, pos + 20, W_PAWN2);
        check_wpawn_eat(pos, pos + 9);
        check_wpawn_eat(pos, pos + 11);
        break;
    case B_PAWN:
        if (check_pawn_move(pos, pos - 10, 0) && pos >= 60)
            check_pawn_move(pos, pos - 20, B_PAWN2);
        check_bpawn_eat(pos, pos - 9);
        check_bpawn_eat(pos, pos - 11);
        break;
    }
}

//------------------------------------------------------------------------------------
// Test both check and check & mat
//------------------------------------------------------------------------------------

static int in_mat(int side)
{
    int from, check;
    move_t list_of_moves[256];
    move_t *m;

    // List all possible moves
    move_ptr = list_of_moves;
    for (from = 0; from < BOARD_SIZE; from++)
        if (B(from) & side) list_moves(from);

    // set the board with each possible move and check if the king is in check
    for (m = list_of_moves; m < move_ptr; m++) {
        do_move(*m);
        check = in_check(side, king_pos[play + 1]);
        undo_move();
        if (!check) return CHECK_GS;  // in check but not mat
    }
    return MAT_GS;  // mat
}

static int in_check_mat(int side)
{
    if (!in_check(side, king_pos[play])) return WAIT_GS;  // not even in check
    return in_mat(side);
}

//------------------------------------------------------------------------------------
// Do the move, but only if it is legal
//------------------------------------------------------------------------------------

static int try_move(move_t move, int side) {

    // Notice the piece type (for mv50)
    char type = B(move.from) & TYPE;

    // Try the move, then reject it if it puts the same side king in check
    do_move(move);
    if (in_check( side, king_pos[play + 1])) {
        undo_move();
        return 0;
    }

    // The move was fully legal, accept it
    nb_plays = play;

    // Update the nb of "steril" moves
    mv50 = (type == PAWN || move.eaten) ? 0 : mv50 + 1;

    return 1;
}

int try_move_str(char *move_str)
{
    move_t move;
    if (str_to_move(move_str, &move) == 0) return -1;

    // Side check
    int side = (play & 1) ? BLACK : WHITE;
    if ((B(move.from) & COLORS) != side) return 0;

    // Verify if it is a pseudo-legal one
    move_t list_of_moves[28];
    move_ptr = list_of_moves;
    list_moves(move.from);

    move_t *m;
    for (m = list_of_moves; m < move_ptr; m++) if (m->val == move.val) break;
    if (m == move_ptr) return 0;

    if (!try_move(move, side)) return 0;
    log_info_va("Play %d: <- %s\n", play, move_str);
    return 1;
}

//------------------------------------------------------------------------------------
// Board Evaluation
//------------------------------------------------------------------------------------

static int evaluate(int side, int a, int b)
{
    int sq, piece, res;

    // Take the total of the values of the pieces present on the board
    res = board_val[play];

    // If a piece has been eaten at the horizon, this is risky, so
    // remove half of the value of the eating piece
    if (moved[play - 1].eaten)
        res -= piece_value[(int)B(moved[play - 1].to)] / 2;

    if (side == BLACK) {
        if (res > b + 170 || res < a - 170) return res;
    }
    else {
        if (-res > b + 170 || -res < a - 170) return -res;
    }

    // Center occupation
    for (sq = 0; sq < 78; sq++) {
        piece    = B(sq);
        int type = piece & TYPE;
        //        if (type == QUEEN || type == ROOK || type == KING) continue;
        if (type == KING) continue;
        if (piece & BLACK) {
            res += black_pos_bonus[sq];  // (will be inverted if side = WHITE)

            // Discourage own piece in front of pawn (including double pawn)
            if (piece == B_PAWN && B(sq - 10) & BLACK) res -= 9;
        }
        else if (piece & WHITE) {
            res -= white_pos_bonus[sq];
            if (piece == W_PAWN && B(sq + 10) & WHITE) res += 9;
        }
    }

    // Evaluations when there are few pieces
    if (nb_pieces[play] < 24) {
        for (sq = 0; sq < BOARD_SIZE - 2; sq++) {
            piece = B(sq);

            // Encourage pawns to advance to get promotion at end of game
            if (piece == W_PAWN) res -= (sq / 10) << 3;
            else if (piece == B_PAWN) res += (7 - (sq / 10)) << 3;

            // Discourage king to go in a corner
            else if (piece == W_KING) res += king_pos_malus[sq];
            else if (piece == B_KING) res -= king_pos_malus[sq];
        }
    }
    return (side == BLACK) ? res : -res;
}

//------------------------------------------------------------------------------------
// Transposition Table management
//------------------------------------------------------------------------------------

static int nb_dedup;
static int nb_hash;

static int get_table_entry(int depth, int side, int* flag, int* eval)
{
    move_t move;

    // compute the hash of the board. Seed = castles + en_passant location...
    uint32_t seed = (en_passant[play] << 24) + (castles[play] << 16) + (castles[play + 1] << 8) + (play & 1);
    uint64_t hash = pengyhash((void *)board_ptr, BOARD_SIZE - 2, seed);

    // Look if the hash is in the transposition table
    int h = hash & (TABLE_ENTRIES-1);
    if ((table[h].hash ^ hash) < TABLE_ENTRIES) {

        // To reduce hash collisions, reject an entry with impossible move
        move = table[h].move;
        if ((B(move.from) & COLORS) == side && B(move.to) == move.eaten) {

            // Only entries with same depth search are usable, but a move
            // from other depth search is interesting (example: PV move)
            *flag =  (table[h].depth == depth) ? table[h].flag : OTHER_DEPTH;

            *eval = table[h].eval;
            return h;
        }
    }

    // The hash was not present or was for another board. Set the table entry
    table[h].hash     = hash;
    table[h].move.val = 0;
    *flag             = NEW_BOARD;
    nb_hash++;
    return h;
}

//------------------------------------------------------------------------------------
// Sort moves in descending order of interest to improve alpha-beta prunning
//------------------------------------------------------------------------------------

static void fast_sort_moves(move_t* list, int nb_moves, int level, move_t table_move)
{
    // Indexes of attacks in the sparsely filled sorted_attacks[] list.
    // Attacks ordering is "most valuable victim by least valuable attacker" first.
    // Index starts at 3 to leave place for the PV move and 2 killer moves.
    // Use as follows: attack_indexes[8*attacker_type + victim_type]. Up to 2x 3 queens
    int attack_indexes[64] = {
        0, 0, 0, 0, 0, 0, 0, 0, 
        0, 0, 0, 0, 0, 0, 0, 0, 
        0, 0, 98, 3, 76, 55, 33, 3, 
        0, 0, 156, 3, 96, 75, 53, 32, 
        0, 0, 112, 3, 80, 59, 37, 9, 
        0, 0, 122, 3, 84, 63, 41, 15, 
        0, 0, 130, 3, 86, 65, 43, 18, 
        0, 0, 134, 3, 90, 69, 47, 23  }; // sparse attack list minimal size: 163

    #define LAST_ATTACK_INDEX ((KING<<3) + PAWN)

    unsigned int sorted_attacks[192] = { 0 };
    unsigned int other_moves[256];
    int i, i_max, a, m, o;

    // get all move scores
    for (m = 0, o = 0; m < nb_moves; m++) {

        unsigned int val = list[m].val;

        // Give the 1st rank to the Transition Table move (PV move or other)
        if (val == table_move.val)
            sorted_attacks[0] = val;

        // Give the 2nd rank to the previous best move at this level (the "killer" move)
        else if (val == best_move[level].val)
            sorted_attacks[1] = val;

        // Give the 2nd rank to the previous best move at this level (the "killer" move)
        else if (val == next_best[level].val)
            sorted_attacks[2] = val;

        // place the attacking moves in a sparsely filled, but ordered list
        else if (list[m].eaten) {
            // get the index in the attack_indexes[] table (8*attacker + victim) 
            i = ((B(list[m].from) & 7)<<3) + (list[m].eaten & 7);
            // sorted_attacks[i] is the place for this attack
            sorted_attacks[attack_indexes[i]++] = val;
        }
        // place the non attacking moves in another list
        else other_moves[o++] = val;
    }
    // Compact the sorted attacks list
    i_max = attack_indexes[LAST_ATTACK_INDEX];
    for (i = 0, a = 0; i < i_max; i++) if (sorted_attacks[i]) sorted_attacks[a++] = sorted_attacks[i];

    // Finally put back the sorted attacks and the other moves in the input list
    if (a) memcpy(list, sorted_attacks, a*sizeof(int));
    if (o) memcpy(list + a, other_moves, o*sizeof(int));
}

//------------------------------------------------------------------------------------
// The min-max recursive algo with alpha-beta pruning
//------------------------------------------------------------------------------------

static int level_max;
static int ab_moves, next_ab_moves_time_check;

static int nega_alpha_beta(int level, int a, int b, int side, move_t *upper_sequence)
{
    int i, p, from, check, flag, eval, max = -300000, one_possible = 0;
    move_t list_of_moves[256];
    move_t sequence[LEVEL_MAX];
    move_t *m;
    move_t mm_move;
    mm_move.val = 0;

    if ((check = in_check_mat(side)) == MAT_GS) return max;

    // Last level: evaluate the board
    int depth = level_max - level;
    if (depth == 0) return evaluate(side, a, b);

    // Search the board in the transposition table
    int h = get_table_entry(depth, side, &flag, &eval);

    int old_a = a;
    if (flag == LOWER_BOUND)      { if (a < eval) a = eval;  }
    else if (flag == UPPER_BOUND) { if (b > eval) b = eval;  }
    if (flag == EXACT_VALUE || (a >= b && flag > OTHER_DEPTH)) {
        nb_dedup++;
        mm_move          = table[h].move;
        next_best[level] = best_move[level];
        best_move[level] = mm_move;
        sequence[level]  = mm_move;
        memcpy(upper_sequence, sequence, level_max * sizeof(move_t));  // reductible...
        return eval;
    }

    // List all the pseudo legal moves
    move_ptr = list_of_moves;
    if (randomize) {
        from = (((int)__rdtsc()) & 0x7FFFFFFF) % (BOARD_SIZE - 2);
        for (i = 0; i < BOARD_SIZE - 2; i++, from++) {
            if (from == BOARD_SIZE -2) from = 0;
            if (B(from) & side) list_moves(from);
        }
    }
    else {
        for (from = 0; from < BOARD_SIZE - 2; from++)
            if (B(from) & side) list_moves(from);
    }
    int nb_of_moves = move_ptr - list_of_moves;
    if (nb_of_moves == 0)
        return (side == engine_side) ? -100000 : 100000; // Avoid "Pats"
    move_ptr->val = 0;

    // List king protectors: no king check verification will be done on non-protectors moves
    int king_protectors[8];
    int king_protectors_nb = 0;
    if (!check) king_protectors_nb = list_king_protectors(side, &king_protectors[0]);

    // Set the Futility level
    int futility = 300000;  // by default, no futility
    if (depth == 1 && !check && nb_pieces[play] > 23)
        futility = 50 + ((side == BLACK) ? board_val[play] : -board_val[play]);

    // Sort the moves to maximize alpha beta pruning efficiency
    fast_sort_moves(list_of_moves, nb_of_moves, level, table[h].move);

    // Try each possible move
    for (m = list_of_moves; m->val; m++) {

        // Once the max level reached, only evaluate "eating" moves. We'll stop
        // on a "stablized" eating situation that will provide a better evaluation (algo 2)
        //        if (level > level_max && B(m->to) == 0) continue;

        // Futility pruning
        if (futility < max && one_possible && B(m->to) == 0) continue;

        // set the board with this possible move
        do_move(*m);

        // do not allow the move if it puts (or leaves) the own king in check
        if (!check) {
            // No need to check check for moves from pieces that don't protect the king
            for (p = 0; p < king_protectors_nb && m->from != king_protectors[p]; p++) continue;
            if (p == king_protectors_nb) goto after_check_check;
        }
        if (in_check(side, king_pos[play + 1])) {
            undo_move();
            continue;
        }
    after_check_check:

        // evaluate this move
        if (one_possible == 0) {
            one_possible = 1;
            eval = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }
        else {
            eval = -nega_alpha_beta(level + 1, -a - 1, -a, side ^ COLORS, sequence);
            if (a < eval && eval < b && depth > 2)
                eval = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }

        // undo the move to evaluate the others
        undo_move();

        // Every 10000 moves, look at elapsed time
        if (++ab_moves > next_ab_moves_time_check) {
            // if time's up, stop search and keep previous lower depth search move
            if (get_chrono() >= time_budget_ms) return -400000;
            next_ab_moves_time_check = ab_moves + 10000;
        }

        // Penalty on certain 1st moves
        if (level == 0) {
            char type = B(m->from) & TYPE;

            // Discourage too many successive "sterile" moves (rule is 50 max)
            if (mv50 > 24 && type != PAWN && !m->eaten) eval -= mv50;

            // Discourage king move and rook move at the beginning of the game
            if (type == KING) eval -= 8;
            if (type >= ROOK && play < 10) eval -= 20;

            // Discourage move back and perpetual move loops
            if (play > 6) {
                if (m->from == moved[play - 2].to   && m->to == moved[play - 2].from) eval -= 10;
                if (m->from == moved[play - 4].from && m->to == moved[play - 4].to  ) eval -= 30;
                if (m->from == moved[play - 6].to   && m->to == moved[play - 6].from) eval -= 100;
                if (play > 12) {
                    if (m->from == moved[play -  8].from && m->to == moved[play -  8].to) eval -= 300;
                    if (m->from == moved[play - 12].from && m->to == moved[play - 12].to) eval -= 600;
                }
            }
        }

        // The player wants to maximize his score
        if (eval > max) {
            max              = eval;  // max = max( max, eval )
            mm_move          = *m;
            next_best[level] = best_move[level];
            best_move[level] = mm_move;
            sequence[level]  = mm_move;
            memcpy(upper_sequence, sequence, level_max * sizeof(move_t));

            if (max >= b) goto end_add_to_tt;
            if (max > a) a = max;
        }
    }
    if (one_possible == 0)
        return (side == engine_side) ? -100000 : 100000;  // Avoid "Pats"

end_add_to_tt:
    table[h].eval     = max;
    table[h].move.val = mm_move.val;
    table[h].depth    = depth;
    if  (max <= old_a) table[h].flag = UPPER_BOUND;
    else if (max >= b) table[h].flag = LOWER_BOUND;
    else               table[h].flag = EXACT_VALUE;
    return max;
}

//------------------------------------------------------------------------------------
// The compute engine : how we'll call the min-max recursive algo
//------------------------------------------------------------------------------------

void compute_next_move(void)
{
    move_t engine_move;

    engine_side = (play & 1) ? BLACK : WHITE;

    // Verify the situation...
    if (in_check_mat(engine_side) == MAT_GS) {
        game_state = LOST_GS;
        return;
    }

    // Don't waist time thinking for the 1st move.
    if (!use_book && play == 0) {
        engine_move.val = first_ply[rand() % 6].val;
        goto play_the_prefered_move;
    }

    // Optionally consult the opening moves book.
    else if (use_book && play < 16) {
        // compute the board hash. Seed = castles + en_passant + color to play...
        uint32_t seed = (en_passant[play] << 24) + (castles[play] << 16) + (castles[play + 1] << 8) + (play & 1);
        uint64_t hash = pengyhash((void *)board_ptr, BOARD_SIZE - 2, seed);
#ifdef __MINGW32__
        log_info_va("Look in book hash 0x%0llX : ", hash);
#else
        log_info_va("Look in book hash 0x%0lX : ", hash);
#endif

        int b, ns = 1;
        for (b = hash & BOOK_MSK; hash != book[b].hash && book[b].nb_moves; b = (b + 1) & BOOK_MSK, ns++) continue;
        if (hash == book[b].hash) {
            log_info_va("found at index %d (%d searches)\n", b, ns);
            engine_move.val = book[b].move[rand() % book[b].nb_moves];
            goto play_the_prefered_move;
        }
        log_info("not found\n");
    }

    long level_ms = 0, elapsed_ms = 0;
    start_chrono();

    // Search deeper and deeper the best move,
    // starting with the previous "best" move to improve prunning
    level_max       = 0;
    engine_move.val = 0;

    do {
        best_move[level_max].val = 0;
        next_best[level_max].val = 0;
        level_max++;
        ab_moves                 = 0;
        next_ab_moves_time_check = ab_moves + 10000;
        nb_dedup                 = 0;
        nb_hash                  = 0;

        int max = nega_alpha_beta(0, -400000, 400000, engine_side, best_sequence);
        engine_move = best_sequence[0];
        if (engine_move.val == 0) {
            game_state = PAT_GS;
            return;
        }

        level_ms = -elapsed_ms;
        elapsed_ms = get_chrono();
        level_ms += elapsed_ms;

        if (verbose) {
            send_str_va("%2d %7d %4ld %8d ", level_max, max, elapsed_ms / 10, ab_moves);
            for (int l = 0; l < level_max && l < 13; l++)
                send_str_va(" %s", move_str(best_sequence[l]));
            send_str("\n");
        }

        // If a check-mat is un-avoidable, no need to think more
        if (max > 199800 || max < -199800) break;

        // Evaluate if we have time for the next search level
        if (level_ms * 3 > time_budget_ms - elapsed_ms) break;
    }
    while (level_max <= LEVEL_MAX);
    total_ms += elapsed_ms;

play_the_prefered_move:
    try_move(engine_move, engine_side);
    engine_move_str = move_str(engine_move);

    log_info_va("Play %d: -> %s\n", play, engine_move_str);
    log_info_va("Table entries created: %d, Deduplications: %d\n", nb_hash, nb_dedup);
    log_info_va("Total think time: %d min %d sec %d ms\n", (int)(total_ms/60000), (int)((total_ms/1000)%60), (int)(total_ms%1000));

    // Return with the opponent side situation
    game_state = in_check_mat(engine_side ^ COLORS);
}
