#include <stdlib.h>
#include <signal.h>
#include "engine.h"

//------------------------------------------------------------------------------------
// Linux / Windows incompatibility wrapping functions
//------------------------------------------------------------------------------------

#ifdef __MINGW32__

#include <windows.h>
#define data_from_stdin() (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE),0)==WAIT_OBJECT_0)

// (need to make output to stdout in separate thread work, to enable run_in_new_thread ...)
//#include <process.h>
//#define run( func ) _beginthread((void (*)(void *))&func, 0, NULL);
#define run( func ) func()

#else

#include <poll.h>
struct pollfd input[1] = {{.fd=0, .events=POLLIN}}; // Event to poll will be input on stdin (fd 0)
#define data_from_stdin() poll(input, 1, 0)

#include <pthread.h>
pthread_t thrd;
#define run( func ) \
    pthread_create( &thrd, NULL, (void * (*)(void *))&func, NULL); \
    pthread_detach( thrd )

#endif

//------------------------------------------------------------------------------------
// Communication stuff
//------------------------------------------------------------------------------------

FILE* logfile;

void log_info( const char* str )
{
    fputs( str, logfile );
}

static int send_nl=1;
void send_str( const char* str )
{
    fputs( str, stdout );
    if (send_nl) fputs( "-> ", logfile);
    fputs( str, logfile );
    send_nl = !!strchr( str, '\n' );
}

//------------------------------------------------------------------------------------
// Time budget functions
//------------------------------------------------------------------------------------

static int time_ctrl_inc, moves_in_tc = 0, remaining_moves_in_tc = 0;

static void set_time_ctrl( char* arg)
{
    char time_string[10];
    sscanf( arg, "%d %s %d", &moves_in_tc, time_string, &time_ctrl_inc );
    remaining_moves_in_tc = moves_in_tc;
}

static void set_next_play_time( int ms)
{
    time_budget_ms = ms - 1; // 1 ms margin
    fprintf( logfile, "time per move = %ld ms\n", time_budget_ms );
}

static void budget_next_play_time( int remaining_time_ms)
{
    if (moves_in_tc) {
        if (remaining_moves_in_tc == 0) remaining_moves_in_tc = moves_in_tc;
        set_next_play_time( remaining_time_ms / remaining_moves_in_tc--);
    }
    else if (time_ctrl_inc) set_next_play_time( time_ctrl_inc * 1000);
    else                    set_next_play_time( remaining_time_ms);
}

//------------------------------------------------------------------------------------
// Infinite loop looking at xboard commands received via stdin
//------------------------------------------------------------------------------------

void intHandler( int unused )
{
    (void) unused;

    fclose( logfile);
    exit( 0 );
}

int main(int argc, char* argv[])
{
    (void) argc;
    
    char* name;
    char cmd[128];
    char* arg;

    // Various initialisations

    int go = 1, prev_state = WAIT_GS;

    if      ((name = strrchr( argv[0], '/'  ))) name++;
    else if ((name = strrchr( argv[0], '\\' ))) name++;
    else      name = argv[0];

    signal( SIGINT, intHandler);

    logfile = fopen("my_log.txt", "w");

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    init_game( NULL );

    while (1) {

        // Handle chess engine state change (most often from THINK_GS to WAIT_GS)
        if (prev_state != game_state) {
            if (game_state <= MAT_GS)
                send_str_va( "move %s\n", engine_move_str );
            if (game_state == MAT_GS || game_state == LOST_GS)
                send_str( (play & 1) ? "0-1 {Black mates}\n" : "1-0 {White mates}\n");
            else if (game_state == PAT_GS)
                send_str( "1/2-1/2 {Stalemate}\n");
            game_state = WAIT_GS;
            prev_state = WAIT_GS;
        }

        // Get a message from stdin
        if (data_from_stdin() == 0)          continue;
        if (fgets( cmd, 127, stdin) == NULL) continue;
        if (strlen( cmd ) < 2)               continue;

        fprintf( logfile, "<- %s", cmd );

        // Remove '\n' at the end of the message
        arg = strchr( cmd, '\n'); if (arg) *arg = 0;

        // Separate the command from its argument(s) in the message
        arg = strchr( cmd, ' ');  if (arg) *(arg++) = 0;

        // Handle supported non-move xboard commands
        if (!strcmp(cmd, "protover")) {
            send_str("feature myname=\""); send_str(name); send_str("\"\n");
            send_str("feature ping=1\n");
            send_str("feature sigint=0\n");
            send_str("feature sigterm=0\n");
            send_str("feature variants=\"normal\"\n");
            send_str("feature done=1\n");
        }
        else if (!strcmp(cmd, "ping"))     send_str_va( "pong %s\n", arg);
        else if (!strcmp(cmd, "new"))    { init_game( NULL ); go = 1; }
        else if (!strcmp(cmd, "quit"))     break;
        else if (!strcmp(cmd, "force"))    go = 0;
        else if (!strcmp(cmd, "go"))     { go = 1; game_state = THINK_GS; }
        else if (!strcmp(cmd, "sd"))     { level_max_max = atoi(arg); if (level_max_max > LEVEL_MAX) level_max_max = LEVEL_MAX; }
        else if (!strcmp(cmd, "post"))     verbose = 1;
        else if (!strcmp(cmd, "nopost"))   verbose = 0;
        else if (!strcmp(cmd, "setboard")) init_game( arg );
        else if (!strcmp(cmd, "undo"))     user_undo_move();
        else if (!strcmp(cmd, "random"))   randomize = 1 - randomize;
        else if (!strcmp(cmd, "level"))    set_time_ctrl( arg);
        else if (!strcmp(cmd, "time"))     budget_next_play_time( atoi(arg) * 10);
        else if (!strcmp(cmd, "st"))       set_next_play_time( atoi(arg) * 1000);

        // Silently ignore the following xboard commands
        else if (
            !strcmp(cmd, "result")   || // TODO
            !strcmp(cmd, "name")     || // TODO
            !strcmp(cmd, "computer") || // TODO
            !strcmp(cmd, "black")    ||
            !strcmp(cmd, "white")    ||
            !strcmp(cmd, "xboard")   ||
            !strcmp(cmd, "accepted") ||
            !strcmp(cmd, "easy")     ||
            !strcmp(cmd, "hard")     ||
            !strcmp(cmd, "hint")     ||
            !strcmp(cmd, "otim")     ||    
            !strcmp(cmd, "rejected") ) continue;

        // Handle a move or an unknown xboard command
        else {
            int res = try_move_str( cmd );
            if      (res <  0)  send_str_va("Error (unknown command): %s\n", cmd);
            else if (res == 0)  send_str_va("Illegal move: %s\n", cmd);
            else if (go)        game_state = THINK_GS;
        }

        // Let the chess engine play (in another thread under linux)
        if (game_state == THINK_GS && prev_state != THINK_GS) {
            prev_state = THINK_GS;
            run( compute_next_move );
        }
    }
    return 0;
}

