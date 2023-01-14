#ifndef _ENGINE
#define _ENGINE

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Common definitions

// game states
#define WAIT_GS  0
#define CHECK_GS 1
#define MAT_GS   2
#define WIN_GS   2
#define LOST_GS  3
#define PAT_GS   4
#define THINK_GS 5
#define ANIM_GS  6
#define QUIT_GS  7

#define LEVEL_MAX 63

// Common variables : game settings

extern int   use_book;
extern int   verbose;
extern int   randomize;
extern int   level_max_max;
extern int   trace;

// Common variables : game current state

extern int   game_state;
extern char* engine_move_str;
extern int   play;
extern int   nb_plays;
extern long  time_budget_ms;

// Chess engine functions

void  init_game( char* FEN_string );
int   try_move_str( char *move_str );
void  compute_next_move( void );

// Play interface helper functions

void  set_piece( char ch, int l, int c);
char  get_piece( int l, int c);
void  user_undo_move( void );
void  user_redo_move( void );
void  set_possible_moves_board( int l, int c);
char  get_possible_moves_board( int l, int c);
char* get_move_str( int play);
void  set_FEN_end( char *ptr, char separator);

void log_info( const char* str );
void send_str( const char* str );

#define log_info_va( ... ) do { char str_va[64]; sprintf( str_va, __VA_ARGS__); log_info(str_va); } while(0)
#define send_str_va( ... ) do { char str_va[64]; sprintf( str_va, __VA_ARGS__); send_str(str_va); } while(0)

#endif
