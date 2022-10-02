#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Chess pieces
enum piece_t {
    TYPE   =  7,   PAWN =  2,   KING =  3,   KNIGHT =  4,   BISHOP =  5,   ROOK =  6,   QUEEN =  7,
    WHITE  =  8, W_PAWN = 10, W_KING = 11, W_KNIGHT = 12, W_BISHOP = 13, W_ROOK = 14, W_QUEEN = 15,
    BLACK  = 16, B_PAWN = 18, B_KING = 19, B_KNIGHT = 20, B_BISHOP = 21, B_ROOK = 22, B_QUEEN = 23,
    COLORS = 24,   STOP = 32 };

// A chess board is the 64 squares playable area + a 2-squares thick border around it.
#define BOARD_AND_BORDER_SIZE 100
#define BOARD_SIZE             80 // Smallest contiguous area we need to copy
#define FIRST_BOARDER_SIZE     32 // 32 instead of 21 or 22 for mem aligment
#define NO_POSITION            79 // an impossible position
#define MAX_TURNS             500

char boards[FIRST_BOARDER_SIZE + BOARD_AND_BORDER_SIZE*MAX_TURNS] __attribute__((aligned (16)));

#define BOARD0 &boards[FIRST_BOARDER_SIZE]
char* board_ptr = BOARD0;
#define B(x) (*(board_ptr + (x)))

// Possible place to eat "en passant" a pawn that moved two rows.
char en_passant[MAX_TURNS];

// Castle rules: involved rook and king must not have moved before the castle
#define LEFT_CASTLE  1
#define RIGHT_CASTLE 2
#define ALL_CASTLES  3
char castles[MAX_TURNS+2]; // (even index: white castle, odd index: black castle)

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
#define W_PAWN2   10

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

int play        = 0;

char initial_board[BOARD_SIZE] = {
    W_ROOK, W_KNIGHT, W_BISHOP, W_QUEEN, W_KING, W_BISHOP, W_KNIGHT, W_ROOK, STOP, STOP,
    W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, STOP, STOP,
    0, 0, 0, 0, 0, 0, 0, 0, STOP, STOP,
    0, 0, 0, 0, 0, 0, 0, 0, STOP, STOP,
    0, 0, 0, 0, 0, 0, 0, 0, STOP, STOP,
    0, 0, 0, 0, 0, 0, 0, 0, STOP, STOP,
    B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, STOP, STOP,
    B_ROOK, B_KNIGHT, B_BISHOP, B_QUEEN, B_KING, B_BISHOP, B_KNIGHT, B_ROOK, STOP, STOP
};

char piece_char[17] = ".?pknbrq??PKNBRQ";

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------

void init_game(void)
{
    memset( boards, STOP, sizeof(boards) );
    memcpy( BOARD0, initial_board, BOARD_SIZE );

    en_passant[0] = NO_POSITION;
    en_passant[1] = NO_POSITION;
    castles[0] = ALL_CASTLES;
    castles[1] = ALL_CASTLES;

    board_ptr  = BOARD0;
    play       = 0;                // index of play iterations
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------

uint64_t pengyhash(const void *p, size_t size, uint32_t seed)
{
    uint64_t b[4] = { 0 };
    uint64_t s[4] = { 0, 0, 0, size };
    int i;

    for(; size >= 32; size -= 32, p = (const char*)p + 32) {
        memcpy(b, p, 32);
        
        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50);
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    memcpy(b, p, size);

    for(i = 0; i < 6; i++) {
        s[1] = (s[0] += s[1] + b[3]) + (s[1] << 14 | s[1] >> 50) + seed;
        s[3] = (s[2] += s[3] + b[2]) + (s[3] << 23 | s[3] >> 41);
        s[3] = (s[0] += s[3] + b[1]) ^ (s[3] << 16 | s[3] >> 48);
        s[1] = (s[2] += s[1] + b[0]) ^ (s[1] << 40 | s[1] >> 24);
    }

    return s[0] + s[1] + s[2] + s[3];
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------

void do_move( struct move_t m)
{
    // Remember the whole previous game to be able to undo the move
    memcpy( board_ptr + BOARD_AND_BORDER_SIZE, board_ptr, BOARD_SIZE);
    board_ptr += BOARD_AND_BORDER_SIZE;

    int piece = B(m.from);

    // if it is a king move, keep track of its position and forbid future castles
    castles [play+2] = ((piece & TYPE) == KING) ? 0 : castles[play];

    moved[play] = m;
    play++;
    en_passant[play] = NO_POSITION;

    B(m.from) = 0;
    B(m.to) = piece;

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
        break;
    case W_PAWN2:
        en_passant[play] = m.from + 10;        // notice "en passant" possibility
        break;
    case B_PAWN2:
        en_passant[play] = m.from - 10;        // notice "en passant" possibility
        break;
    case EN_PASSANT:                           // eat "en passant"
        if (piece == W_PAWN) B(m.to - 10) = 0;
        else                 B(m.to + 10) = 0;
        break;
    case L_ROOK:
        castles[play+1] &= ~LEFT_CASTLE;
        break;
    case R_ROOK:
        castles[play+1] &= ~RIGHT_CASTLE;
        break;
    }
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------

int col, lig;

int try_move( char piece, int from, int to, struct move_t* m)
{
//    printf("  try from %d to %d, B(from) %d, piece %d, col %d, lig %d\n", from,to, B(from), piece,col,lig);

    if (B(from) != piece) return 0;
    if ((col >= 0) && (from % 10) != col) return 0;
    if ((lig >= 0) && (from / 10) != lig) return 0;

    m->from  = from;
    m->to    = to;
    m->eaten = B(to);
    return 1;
}

int find_move_to( char piece, int to, struct move_t* m)
{
    int  p;
    char type   = piece & TYPE;
    int  player = piece & COLORS;

    m->val = 0;

    switch (type) {
    case PAWN:
        if (piece == W_PAWN) {
            if ((B(to) & BLACK) || to == en_passant[play]) {
                if (try_move( piece, to -  9, to, m)) { if (to == en_passant[play]) { m->special = EN_PASSANT; } return 1; }
                if (try_move( piece, to - 11, to, m)) { if (to == en_passant[play]) { m->special = EN_PASSANT; } return 1; }
            }
            else if (B(to) == 0) {
                if (try_move( piece, to - 10, to, m)) return 1;
                if (to >= 30 && to <= 37) if (try_move( piece, to - 20, to, m)) { m->special = W_PAWN2; return 1; }
            }
        }
        else {
            if ((B(to) & WHITE) || to == en_passant[play]) {
                if (try_move( piece, to +  9, to, m)) { if (to == en_passant[play]) { m->special = EN_PASSANT; } return 1; }
                if (try_move( piece, to + 11, to, m)) { if (to == en_passant[play]) { m->special = EN_PASSANT; } return 1; }
            }
            else if (B(to) == 0) {
                if (try_move( piece, to + 10, to, m)) return 1;
                if (to >= 40 && to <= 47) if (try_move( piece, to + 20, to, m)) { m->special = B_PAWN2; return 1; }
            }
        }
        break;

    case KING:
        if (try_move( piece, to +  1, to, m)) return 1;
        if (try_move( piece, to -  1, to, m)) return 1;
        if (try_move( piece, to + 10, to, m)) return 1;
        if (try_move( piece, to - 10, to, m)) return 1;
        if (try_move( piece, to +  9, to, m)) return 1;
        if (try_move( piece, to -  9, to, m)) return 1;
        if (try_move( piece, to + 11, to, m)) return 1;
        if (try_move( piece, to - 11, to, m)) return 1;
        break;
    
    case QUEEN:
    case BISHOP:
        for (p = to + 11; B(p) == 0; p += 11);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to - 11; B(p) == 0; p -= 11);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to + 9; B(p) == 0; p += 9);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to - 9; B(p) == 0; p -= 9);
        if (try_move( piece, p, to, m)) return 1;

        if (type == BISHOP) break;
        
    case ROOK:
        for (p = to + 1; B(p) == 0; p++);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to - 1; B(p) == 0; p--);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to + 10; B(p) == 0; p += 10);
        if (try_move( piece, p, to, m)) return 1;

        for (p = to - 10; B(p) == 0; p -= 10);
        if (try_move( piece, p, to, m)) return 1;
        break;
        
    // test knight
    case KNIGHT:
        if (try_move( piece, to + 21, to, m)) return 1;
        if (try_move( piece, to - 21, to, m)) return 1;
        if (try_move( piece, to + 19, to, m)) return 1;
        if (try_move( piece, to - 19, to, m)) return 1;
        if (try_move( piece, to + 12, to, m)) return 1;
        if (try_move( piece, to - 12, to, m)) return 1;
        if (try_move( piece, to +  8, to, m)) return 1;
        if (try_move( piece, to -  8, to, m)) return 1;
    }
    return 0;
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------

#define show_line() if (trace) { \
 printf("wn %d play %d :\n%s", wn, play, line); \
 for (int ci=0; ci < ptr-line; ci++) printf(" "); \
 printf("^\n"); \
}

#define BOOK_SIZE (4*1024)
#define BOOK_MSK  (BOOK_SIZE-1)
uint64_t book_hash[BOOK_SIZE];
int  book_nb_moves[BOOK_SIZE];
int  book_move[BOOK_SIZE][10];
int  book_reorder[BOOK_SIZE];

void main (void)
{
    memset( book_hash,     0, sizeof(book_hash)    );
    memset( book_nb_moves, 0, sizeof(book_nb_moves));
    memset( book_move,     0, sizeof(book_move)    );
    
    char piece_ch[6] = "PKNBRQ";
    char line[256];
    char* ptr;
    char* end_ptr;
    struct move_t move;
    int wn, len, i, nb_h = 0, m, to, trace = 0;
    char piece;
    
    FILE* f = fopen("openings.txt", "r");
    if (f==NULL) { printf("could not open openings.txt\n"); return; }

    // Get next opening (extraction of 1st line of moves in a game PGN transcript)
    while( fgets( line, 256, f) ) {

        // First test_apply=0: just try the line of moves.
        // If incoherent moves are detected, test_apply will be set to 2 to skip the whole line
        // Otherwise, at 2nd pass, when test_apply=1, compute the hash
        for (int test_apply_skip = 0; test_apply_skip < 2; test_apply_skip++) { 

            init_game();

            for (ptr = line, wn = 0; wn < 32 && *ptr >= ' '; wn++) {

                // compute the board hash. Seed = castles + en_passant + side to play...
                uint32_t seed = (en_passant[play]<<24) + (castles[play]<<16) + (castles[play+1]<<8) + (play & 1);
                uint64_t hash = pengyhash( (void*) board_ptr, BOARD_SIZE-2, seed);

                // A priori, no info about 'from' square is given
                col = -1; lig = -1;
            
                // skip the move nb (after every 2 move)
                if ((wn % 3) == 0) {
                    while (*ptr++ != '.');
                    while (*ptr == ' ') ptr++;
                    wn++;
                }

                // measure move string length (length to first of string end, space, or \n)
                len = strlen(ptr);
                if (end_ptr = strchr( ptr, ' ' )) if (len > end_ptr - ptr) len = end_ptr - ptr;
                if (end_ptr = strchr( ptr, '\n')) if (len > end_ptr - ptr) len = end_ptr - ptr;
                if (end_ptr = strchr( ptr, '\r')) if (len > end_ptr - ptr) len = end_ptr - ptr;

                if (len==0) break;

                char first_ch = *ptr;
                int init_len = len;

                // convert castle moves
                if (len == 5 && !strncmp( ptr, "O-O-O", 5)) {
                    move.from    = (play & 1) ? 74 : 4;
                    move.to      = (play & 1) ? 72 : 2;
                    move.special = (play & 1) ? BL_CASTLE : WL_CASTLE;
                    ptr += 5;
                }
                else if (len == 3 && !strncmp( ptr, "O-O", 3)) {
                    move.from    = (play & 1) ? 74 : 4;
                    move.to      = (play & 1) ? 76 : 6;
                    move.special = (play & 1) ? BR_CASTLE : WR_CASTLE;
                    ptr += 3;
                }

                // find the other moves
                else {

                    // Get the piece letter
                    for (i = 0; i < 6 && *ptr != piece_ch[i]; i++);
                    if (i<6) {
                        piece = (play & 1) ? BLACK + (i+2) : WHITE + (i+2);
                        ptr++; len--;
                    }
                    else piece = (play & 1) ? B_PAWN : W_PAWN;

                    // handle characters after the destination info:
                    // detect promotion and skip +, ++, #, etc
                    while (1) {
                        char ch = *(ptr+len-1);
                        if( ch >= '1' && ch <= '8') break;
                        if (ch >= 'b' && ch <= 'r') move.special = PROMOTE;   // TODO: support promotion to other than qween...
                        len--;
                    }

                    // get optional 'from' row and/or col (skip also 'x')
                    while (len > 2) {
                        if (*ptr >= 'a' && *ptr <= 'h') col = *ptr -'a';
                        if (*ptr >= '1' && *ptr <= '8') lig = *ptr -'1';
                        ptr++; len--;
                    }

                    // get the destination square
                    to  = *ptr++  - 'a';
                    to += 10*(*ptr++ - '1');

                    // skip the characters after the 'to' characters (promotion, +, ++, #, ...)
                    while (*ptr > ' ') ptr++;

                    // find which move corresponds to all this information
                    find_move_to( piece, to, &move );
                
                }
                if (move.val == 0) {
                    printf( "move = 0 for this line !?! init_len %d first_ch %d, piece %d col %d lig %d to %d len %d\n",
                             init_len, first_ch, piece, col, lig, to, len );
                    trace=1; show_line(); trace=0;

                    // skip the rest of the line and dont apply the line
                    *ptr = 0;
                    test_apply_skip=2;
                    break;
                } 

                // we have a move: apply it.
                do_move (move);

                if (test_apply_skip==1) {
                    // Search if the hash is already present.
                    // Start the search at <hash modulo the hash table size>
                    for (i = hash & BOOK_MSK; hash != book_hash[i] && book_nb_moves[i]; i = (i+1) & BOOK_MSK);

                    // if the hash was found, search the move
                    if (hash == book_hash[i]) {
                        for (m = 0; m < 10; m++) {
                            if (move.val == book_move[i][m]) break;

                            // if the move was not found, add it
                            if (book_move[i][m] == 0) {
                                book_move[i][m++] = move.val;
                                book_nb_moves[i] = m;
                                break;
                            }
                        }
                    }
                    // if the hash was not found, add it at the first empty location
                    else if (book_nb_moves[i] == 0) {
                        book_hash[i] = hash;
                        book_move[i][0] = move.val;
                        book_nb_moves[i] = 1;
                        nb_h++;
                    }
                }

                // go to next word
                while (*ptr == ' ') ptr++;
            }
        }
    }
    fclose (f);
 
    printf( "%d different positions found\n", nb_h);

    // Build opening book book.h file

    f = fopen("book.h", "w"); 
    fprintf( f, "struct book_t {\n");
    fprintf( f, "    uint64_t      hash;  // Pengy hash\n");
    fprintf( f, "    int32_t       nb_moves;\n");
    fprintf( f, "    int32_t       move[10];\n");
    fprintf( f, "} book_t;\n");
    fprintf( f, "\n");
    fprintf( f, "#define BOOK_SIZE %d\n", BOOK_SIZE);
    fprintf( f, "#define BOOK_MSK  %d\n", BOOK_SIZE-1);
    fprintf( f, "\n");
    fprintf( f, "struct book_t book[] = {\n");

    for (i=0; i<BOOK_SIZE; i++) {
        fprintf( f, "{0x%016lX, %d, {", book_hash[i], book_nb_moves[i]);
        for (m = 0; m < 9; m++) if (book_move[i][m]) fprintf( f, "0x%X, ",      book_move[i][m]); else fprintf( f, "0, ");
        if (i < BOOK_SIZE-1)    if (book_move[i][9]) fprintf( f, "0x%X}},\n",   book_move[i][9]); else fprintf( f, "0}},\n");
        else                    if (book_move[i][9]) fprintf( f, "0x%X}} };\n", book_move[i][9]); else fprintf( f, "0}} };\n");
    }
    fclose (f);
}    

