#include <sys/time.h>
#include <x86intrin.h>  // for __rdtsc()

#include "common.h"

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

char boards[FIRST_BOARDER_SIZE + BOARD_AND_BORDER_SIZE * MAX_TURNS] __attribute__((aligned(16)));

#define BOARD0 &boards[FIRST_BOARDER_SIZE]
char *board_ptr = BOARD0;
#define B(x)        (*(board_ptr + (x)))
#define Board(l, c) B(10 * (l) + (c))

// Move structure

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

struct move_t {
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

struct move_t moved[MAX_TURNS];
int board_val[MAX_TURNS];
int nb_pieces[MAX_TURNS];

char *engine_move_str;

int game_state = WAIT_GS;

int play            = 0;
int nb_plays        = 0;
int mv50            = 0;
int verbose         = 1;
int use_book        = 1;
int randomize       = 0;
int level_max_max   = 63;
long time_budget_ms = 2000;
char engine_side;

// The first moves we accept to play
struct move_t first_ply[6] = {
    {.from = 12, .to = 32},
    {.from = 13, .to = 33},
    {.from = 14, .to = 34},
    {.from = 15, .to = 35},
    { .from = 1, .to = 22},
    { .from = 6, .to = 25}
};

long total_ms = 0;

struct move_t *move_ptr;

// Move choosen by the chess engine
struct move_t engine_move;

// Possible place to eat "en passant" a pawn that moved two rows.
char en_passant[MAX_TURNS];

// Castle rules: involved rook and king must not have moved before the castle
#define LEFT_CASTLE  1
#define RIGHT_CASTLE 2
#define ALL_CASTLES  3
char castles[MAX_TURNS + 2];  // (even index: white castle, odd index: black castle)

// Keep track of kings positions
int king_pos[MAX_TURNS + 2];  // (even index: white king, odd index: black king)

//                     -   P,   P,    K,   N,   B,   R,   Q
int piece_value[33] = {0, 100, 100, 4000, 300, 314, 500, 900,
                       0, -100, -100, -4000, -300, -314, -500, -900,
                       0, 100, 100, 4000, 300, 314, 500, 900,
                       0, 0, 0, 0, 0, 0, 0, 0, 0};

// During the first plays, try to occupy the center...
int black_pos_bonus[BOARD_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0, 0, 0,
    0, 0, 4, 15, 15, 4, 0, 0, 0, 0,
    0, 0, 10, 8, 8, 10, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, -5, -2, 0, 0, 0, 0, 0, 0, 0};

int white_pos_bonus[BOARD_SIZE] = {
    0, -5, -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 10, 8, 8, 10, 0, 0, 0, 0,
    0, 0, 4, 15, 15, 4, 0, 0, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// At the end of the game, the king must avoid the corners
int king_pos_malus[BOARD_SIZE] = {
    14, 12, 10, 8, 8, 10, 12, 14, 0, 0,
    12, 9, 7, 6, 6, 7, 9, 12, 0, 0,
    10, 7, 4, 2, 2, 4, 7, 10, 0, 0,
    8, 6, 2, 0, 0, 2, 6, 8, 0, 0,
    8, 6, 2, 0, 0, 2, 6, 8, 0, 0,
    10, 7, 4, 2, 2, 4, 7, 10, 0, 0,
    12, 9, 7, 6, 6, 7, 9, 12, 0, 0,
    14, 12, 10, 8, 8, 10, 12, 14, 0, 0};

struct timeval tv0;

// Opening moves book
#include "book.h"

//------------------------------------------------------------------------------------
// Misc conversion functions
//------------------------------------------------------------------------------------

static int str_to_move(char *str, struct move_t *m)
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

char mv_str[8];
static char *move_str(struct move_t m)
{
    memset(mv_str, 0, sizeof(mv_str));
    if (m.val == 0) return "";
    sprintf(mv_str, "%c%c%c%c", 'a' + m.from % 10, '1' + m.from / 10,
            'a' + m.to % 10, '1' + m.to / 10);
    if (m.special == PROMOTE) sprintf(mv_str + 4, "q");
    return mv_str;
}

char piece_char[33] = " .........PKNBRQ..pknbrq.........";

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

long diff_ms(struct timeval x, struct timeval y)
{
    long s, us;
    s  = x.tv_sec - y.tv_sec;
    us = x.tv_usec - y.tv_usec;
    return 1000 * s + us / 1000;
}

//------------------------------------------------------------------------------------
// Pengy hash used for the openings book
//------------------------------------------------------------------------------------

uint64_t pengyhash(const void *p, size_t size, uint32_t seed)
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
    memset(boards, STOP, sizeof(boards));
    if (FEN_string) FEN_to_board(FEN_string);
    else FEN_to_board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    game_state     = WAIT_GS;
    time_budget_ms = 2000;
    total_ms       = 0;
    randomize      = 0;
    level_max_max  = 63;
}

//------------------------------------------------------------------------------------
// Make the move, undo it, redo it
//------------------------------------------------------------------------------------

void do_move(struct move_t m)
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

int qb_inc[4] = {9, 11, -9, -11};
int qr_inc[4] = {10, 1, -10, -1};

int list_king_protectors(int side, int *king_protectors)
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

int in_check(int side, int pos)
{
    char type;
    int p;
    int fwd   = (side == WHITE) ? 10 : -10;
    int other = COLORS ^ side;

    // test pawn and king closeby
    if ((B(pos + fwd - 1) ^ other) <= KING) goto yes;
    if ((B(pos + fwd + 1) ^ other) <= KING) goto yes;

    // king closeby
    if ((B(pos - fwd + 1) ^ other) == KING) goto yes;
    if ((B(pos - fwd - 1) ^ other) == KING) goto yes;
    if ((B(pos + 1) ^ other) == KING) goto yes;
    if ((B(pos - 1) ^ other) == KING) goto yes;
    if ((B(pos + 10) ^ other) == KING) goto yes;
    if ((B(pos - 10) ^ other) == KING) goto yes;

    // test queen and bishop
    for (p = pos + 11; B(p) == 0; p += 11) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) goto yes;

    for (p = pos - 11; B(p) == 0; p -= 11) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) goto yes;

    for (p = pos + 9; B(p) == 0; p += 9) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) goto yes;

    for (p = pos - 9; B(p) == 0; p -= 9) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == BISHOP) goto yes;

    // test queen and rook
    for (p = pos + 1; B(p) == 0; p++) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) goto yes;

    for (p = pos - 1; B(p) == 0; p--) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) goto yes;

    for (p = pos + 10; B(p) == 0; p += 10) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) goto yes;

    for (p = pos - 10; B(p) == 0; p -= 10) continue;
    type = B(p) ^ other;
    if (type == QUEEN || type == ROOK) goto yes;

    // test knight
    if ((B(pos + 21) ^ other) == KNIGHT) goto yes;
    if ((B(pos - 21) ^ other) == KNIGHT) goto yes;
    if ((B(pos + 19) ^ other) == KNIGHT) goto yes;
    if ((B(pos - 19) ^ other) == KNIGHT) goto yes;
    if ((B(pos + 12) ^ other) == KNIGHT) goto yes;
    if ((B(pos - 12) ^ other) == KNIGHT) goto yes;
    if ((B(pos + 8) ^ other) == KNIGHT) goto yes;
    if ((B(pos - 8) ^ other) == KNIGHT) goto yes;

    return 0;
yes:
    return 1;
}

//------------------------------------------------------------------------------------
// List possible moves
//------------------------------------------------------------------------------------

void add_move(int from, int to, int special)
{
    move_ptr->from    = from;
    move_ptr->to      = to;
    move_ptr->eaten   = B(to);
    move_ptr->special = special;
    move_ptr++;
}

int check_move(char blocking, int from, int to)
{
    if (B(to) & blocking) return 0;
    add_move(from, to, 0);
    return (B(to) == 0);
}

void check_crawler_move(char blocking, int from, int to)
{
    if ((B(to) & blocking) == 0) add_move(from, to, 0);
}

int check_rook_move(char blocking, int from, int to)
{
    int special = 0;
    if (from == 0 || from == 70) special = L_ROOK;
    if (from == 7 || from == 77) special = R_ROOK;

    if (B(to) & blocking) return 0;
    add_move(from, to, special);
    return (B(to) == 0);
}

int check_pawn_move(int from, int to, int special)
{
    if (B(to)) return 0;
    if (to < 8 || to >= 70) special = PROMOTE;
    add_move(from, to, special);
    return 1;
}

void check_wpawn_eat(int from, int to)
{
    int special = 0;
    if (B(to) & BLACK) {
        if (to >= 70) special = PROMOTE;
        add_move(from, to, special);
    }
    else if (en_passant[play] == to)
        add_move(from, to, EN_PASSANT);
}

void check_bpawn_eat(int from, int to)
{
    int special = 0;
    if (B(to) & WHITE) {
        if (to < 8) special = PROMOTE;
        add_move(from, to, special);
    }
    else if (en_passant[play] == to)
        add_move(from, to, EN_PASSANT);
}

void list_moves(int pos)
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
    move_ptr->from = NO_POSITION;
}

//------------------------------------------------------------------------------------
// Test both check and check & mat
//------------------------------------------------------------------------------------

int in_check_mat(int side)
{
    int from, check;
    struct move_t list_of_moves[896];
    struct move_t *p;

    if (!in_check(side, king_pos[play])) return WAIT_GS;  // not even in check

    // List all possible moves
    move_ptr = list_of_moves;
    for (from = 0; from < BOARD_SIZE; from++)
        if (B(from) & side) list_moves(from);

    // set the board with each possible move and check if the king is in check
    for (p = list_of_moves; p->from != NO_POSITION; p++) {
        do_move(*p);
        check = in_check(side, king_pos[play + 1]);
        undo_move();
        if (!check) return CHECK_GS;  // in check but not mat
    }
    return MAT_GS;  // mat
}

//------------------------------------------------------------------------------------
// Do the move, but only if it is legal
//------------------------------------------------------------------------------------

int try_move(char *move_str)
{
    struct move_t move;

    if (str_to_move(move_str, &move) == 0) return -1;

    // Very basic checks
    int color = (play & 1) ? BLACK : WHITE;
    if ((B(move.from) & color) != color) return 0;
    if ((B(move.to) & color) == color) return 0;

    // Verify if it is a pseudo-legal one
    struct move_t list_of_moves[28];
    memset(list_of_moves, 0, sizeof(list_of_moves));
    move_ptr = list_of_moves;
    list_moves(move.from);

    struct move_t *m;
    for (m = list_of_moves; m < move_ptr; m++) if (m->to == move.to) break;
    if (m == move_ptr) return 0;

    // Try the move
    do_move(*m);

    // Refuse a move that would put the player own king in check
    if (in_check(color, king_pos[play + 1])) {
        undo_move();
        return 0;
    }

    // The move was fully legal, accept it
    moved[play - 1].val = m->val;
    nb_plays            = play;

    char msg[24];
    sprintf(msg, "Play %d: <- %s\n", play, move_str);
    log_info(msg);
    return 1;
}

//------------------------------------------------------------------------------------
// Board Evaluation
//------------------------------------------------------------------------------------

int evaluate(int side, int a, int b)
{
    int sq, piece, res = 0;

    // Take the total of the values of the pieces present on the board
    res = board_val[play];

    // If a piece has been eaten at the horizon, this is risky, so
    // remove half of the value of the eating piece
    if (moved[play - 1].eaten)
        res -= piece_value[(int)B(moved[play - 1].to)] / 2;

    if (side == BLACK) {
        if (res > b + 200 || res < a - 200) return res;
    }
    else {
        if (-res > b + 200 || -res < a - 200) return -res;
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
            if (piece == W_PAWN && B(sq + 10) & WHITE) res += 9;
        }
        else if (piece & WHITE) {
            res -= white_pos_bonus[sq];
            if (piece == B_PAWN && B(sq - 10) & BLACK) res -= 9;
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
// The min-max recursive algo with alpha-beta pruning
//------------------------------------------------------------------------------------

struct move_t best_sequence[64];
struct move_t last_best_sequence[64];
struct move_t best_move[64];
int level_max;
int ab_moves                 = 0;
int next_ab_moves_time_check = 0;

int nega_alpha_beta(int level, int a, int b, int side, struct move_t *upper_sequence)
{
    int i, p, from, check, max = -300000, eval = 0, one_possible = 0;
    struct move_t list_of_moves[336];
    struct move_t sequence[200];
    struct move_t *m;
    struct move_t move, mm_move;
    mm_move.val = 0;

    if ((check = in_check_mat(side)) == MAT_GS) goto end;

    // Last level: evaluate the board
    int depth = level_max - level;
    if (depth == 0) return evaluate(side, a, b);

    // Penalty on certain 1st moves
    int penalty = 0;
    if (level == 1) {
        char type = B(moved[play - 1].to) & TYPE;
        if (type == PAWN || moved[play - 1].eaten) mv50 = 0;
        else {
            mv50++;
            if (mv50 > 24) penalty += mv50;
        }
        // Discourage king move and rook move at the beginning of the game
        if (type == KING) penalty += 8;
        if (type >= ROOK && play < 10) penalty += 20;

        // Discourage move back and perpetual move loops
        if (play > 7) {
            if (moved[play - 1].from == moved[play - 3].to && moved[play - 1].to == moved[play - 3].from) penalty += 10;
            if (moved[play - 1].from == moved[play - 5].from && moved[play - 1].to == moved[play - 5].to) penalty += 30;
            if (moved[play - 1].from == moved[play - 7].to && moved[play - 1].to == moved[play - 7].from) penalty += 100;
            if (play > 12) {
                if (moved[play - 1].from == moved[play - 9].from && moved[play - 1].to == moved[play - 9].to) penalty += 300;
                if (moved[play - 1].from == moved[play - 13].from && moved[play - 1].to == moved[play - 13].to) penalty += 600;
            }
        }
    }

    // Try first the move from previous best sequence
    if (last_best_sequence[level].val) {
        mm_move                       = last_best_sequence[level];
        last_best_sequence[level].val = 0;  // don't replay it on other sequences. It may be invalid !
        do_move(mm_move);
        ab_moves++;
        max = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence) + penalty;
        undo_move();
        best_move[level] = mm_move;
        sequence[level]  = mm_move;
        memcpy(upper_sequence, sequence, level_max * sizeof(move_t));  // reductible...

        if (max >= b) goto end;
        if (max > a) a = max;  // a = max( a, max )
        one_possible = 1;
    }

    int king_protectors[8];
    int king_protectors_nb = 0;
    if (!check) king_protectors_nb = list_king_protectors(side, &king_protectors[0]);

    // List all the pseudo legal moves
    move_ptr = list_of_moves;
    for (from = 0; from < BOARD_SIZE - 2; from++)
        if (B(from) & side) list_moves(from);
    int nb_of_moves = move_ptr - list_of_moves;
    if (nb_of_moves == 0) return (side == engine_side) ? -100000 : 100000; // Avoid "Pats"

    // search the previous best move at this level in the list (the "killer" move)
    if (best_move[level].val && best_move[level].val != mm_move.val) {
        move.val = best_move[level].val;
        for (i = 0; i < nb_of_moves; i++)
            // if the "killer" move is one of the moves, try it first
            if (list_of_moves[i].val == move.val) goto found;
    }
    // use __rdtsc() to randomly pick a first move in the list of possible ones
    if (randomize) i = (((int)__rdtsc()) & 0x7FFFFFFF) % nb_of_moves;
    else i = 0;

found:
    m = list_of_moves + i;

    // Set the Futility level
    int futility = 300000;  // by default, no futility
    if (depth == 1 && !check && nb_pieces[play] > 23)
        futility = 50 + ((side == BLACK) ? board_val[play] : -board_val[play]);

    // Try each possible move
    for (i = 0; i < nb_of_moves; i++, m++) {
        if (m->from == NO_POSITION) m = list_of_moves;

        if (m->val == mm_move.val) continue;  // skip move already tried

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
        ab_moves++;

        // evaluate this move
        if (one_possible == 0) {
            one_possible = 1;
            eval         = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }
        else {
            eval = -nega_alpha_beta(level + 1, -a - 1, -a, side ^ COLORS, sequence);
            if (a < eval && eval < b && depth > 2)
                eval = -nega_alpha_beta(level + 1, -b, -a, side ^ COLORS, sequence);
        }
        eval += penalty;

        // undo the move to evaluate the others
        undo_move();

        // The player wants to maximize his score
        if (eval > max) {
            max              = eval;  // max = max( max, eval )
            mm_move          = *m;
            best_move[level] = mm_move;
            sequence[level]  = mm_move;
            memcpy(upper_sequence, sequence, level_max * sizeof(move_t));

            if (max >= b) goto end;
            if (max > a) a = max;
        }

        // Every 10000 moves, look at elapsed time
        if (ab_moves > next_ab_moves_time_check) {
            struct timeval tv1;
            gettimeofday(&tv1, NULL);
            // if time's up, stop search and keep previous lower depth search move
            if (diff_ms(tv1, tv0) > time_budget_ms) return max;
            next_ab_moves_time_check = ab_moves + 10000;
        }
    }
    if (one_possible == 0)
        max = (side == engine_side) ? -100000 : 100000;  // Avoid "Pats"

end:
    if (level == 0 && mm_move.val) engine_move = mm_move;
    return max;
}

//------------------------------------------------------------------------------------
// The compute engine : how we'll call the min-max recursive algo
//------------------------------------------------------------------------------------

void compute_next_move(void)
{
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
        char str[64];
#ifdef __MINGW32__
        sprintf(str, "Look in book hash 0x%0llX : ", hash);
#else
        sprintf(str, "Look in book hash 0x%0lX : ", hash);
#endif
        log_info(str);

        int b, ns = 1;
        for (b = hash & BOOK_MSK; hash != book[b].hash && book[b].nb_moves; b = (b + 1) & BOOK_MSK, ns++) continue;
        if (hash == book[b].hash) {
            sprintf(str, "found at index %d (%d searches)\n", b, ns);
            log_info(str);
            engine_move.val = book[b].move[rand() % book[b].nb_moves];
            goto play_the_prefered_move;
        }
        log_info("not found\n");
    }

    struct timeval tv1;
    long elapsed_ms;
    gettimeofday(&tv0, NULL);

    // Search deeper and deeper the best move,
    // starting with the previous "best" move to improve prunning
    level_max       = 0;
    engine_move.val = 0;
    memset(best_move, 0, sizeof(best_move));

    do {
        last_best_sequence[level_max].val = 0;
        level_max++;
        ab_moves                 = 0;
        next_ab_moves_time_check = ab_moves + 10000;

        int max = nega_alpha_beta(0, -400000, 400000, engine_side, best_sequence);
        if (engine_move.val == 0) {
            game_state = PAT_GS;
            return;
        }

        gettimeofday(&tv1, NULL);
        elapsed_ms = diff_ms(tv1, tv0);

        if (verbose) {
            char str[64];
            sprintf(str, "%2d %7d %4ld %8d ", level_max, max, elapsed_ms / 10, ab_moves);
            send_str(str);
            for (int l = 0; l < level_max && l < 13; l++) {
                send_str(" ");
                send_str(move_str(best_sequence[l]));
            }
            send_str("\n");
        }

        // If a check-mat is un-avoidable, no need to think more
        if (max > 199800 || max < -199800) break;

        memcpy(last_best_sequence, best_sequence, level_max * sizeof(move_t));  // reductible...
    } while (elapsed_ms < (time_budget_ms / 2) && level_max <= level_max_max);
    total_ms += elapsed_ms;

play_the_prefered_move:
    do_move(engine_move);
    nb_plays        = play;
    engine_move_str = move_str(engine_move);
    char msg[24];
    sprintf(msg, "Play %d: -> %s\n", play, engine_move_str);
    log_info(msg);

    // Return with the opponent side situation
    game_state = in_check_mat(engine_side ^ COLORS);
}
