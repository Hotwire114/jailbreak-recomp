/* Online from the game's own menus: the link between the running game and the
 * launcher that started it. LGPL-2.1-or-later. Original author code.
 *
 * The launcher keeps running while the game plays. For each game it starts it
 * makes one block of shared memory named after the game's process id. The
 * launcher's own Online pages (lobby_page.h: rooms, codes, the public lobby,
 * chat) do all the work; the game only shows them in its style and sends the
 * player's input:
 *   launcher -> game   what the page shows (rows, help, chat, notices), a
 *                      question to ask (a room code, yes / no), and "closing"
 *                      when a race starts: the game closes, the launcher starts
 *                      the online game, and afterwards the offline game again;
 *   game -> launcher   commands (open, close, a key, a typed character, a row
 *                      pointed at, an answer).
 * The snapshot is written under a sequence count (odd while writing); commands
 * go through a ring the game fills and the launcher empties. */
#pragma once
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#define OL_ROWS 16
#define OL_CHATS 24
#define OL_CMDS 32
enum {OL_OPEN=1,OL_CLOSE,OL_KEY,OL_CHAR,OL_SELECT,OL_ANSWER};
/* Questions: 1 room code to join, 2 port, 3 sign out (yes / no), 4 remove a member (yes / no). */
typedef struct {int kind,value;wchar_t text[100];} OnlineCmd;
typedef struct {
    volatile LONG seq;                 /* launcher: even when the fields below are whole */
    int open;                          /* 1 while the launcher is on an Online page for the game */
    int page,rows,selected,closing;    /* lobby_page.h page; closing: a race is starting */
    int kind[OL_ROWS];                 /* the launcher's ROW_* kinds (ROW_INFO rows can't be chosen) */
    wchar_t title[32],label[OL_ROWS][64],value[OL_ROWS][64],help[700],notice[256],refused[200];
    int chat_on,chats;wchar_t chat_from[OL_CHATS][64],chat_text[OL_CHATS][256],draft[201];
    int ask,ask_yesno,ask_max;wchar_t ask_title[64],ask_caption[128],ask_value[100];
    int online_count,mmr,mmr_last,mmr_races,signed_in;wchar_t account[64];   /* -1 count: not known (not signed in / not listed yet) */
    int tile_rank,tile_sel;wchar_t tile[4][40];   /* Bikes page: the lit rank's four bikes, the chosen one (-1 Random) */
    volatile LONG cmd_head;LONG cmd_tail;OnlineCmd cmd[OL_CMDS];   /* game writes head, launcher tail */
} OnlineLink;
static void online_link_name(wchar_t*out,size_t cap,DWORD game_pid){_snwprintf(out,cap,L"Local\\RoadRashJailbreakOnline-%lu",(unsigned long)game_pid);out[cap-1]=0;}
