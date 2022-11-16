//
// Copyright (C) 1993-1996 Id Software, Inc.
// Copyright (C) 2016-2017 Alexey Khokholov (Nuke.YKT)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//  DOOM main program (D_DoomMain) and game loop (D_DoomLoop),
//  plus functions to determine game mode (shareware, registered),
//  parse command line parameters, configure game parameters (turbo),
//  and call the startup functions.
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <dos.h>
#include <io.h>
#include "version.h"
#include "doomdef.h"
#include "doomstat.h"

#include "dstrings.h"
#include "sounds.h"

#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"

#include "f_finale.h"
#include "f_wipe.h"

#include "i_random.h"
#include "m_misc.h"
#include "m_menu.h"

#include "i_system.h"
#include "i_sound.h"
#include "i_ibm.h"

#include "g_game.h"

#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"

#include "p_setup.h"
#include "r_local.h"

#include "d_main.h"

#include "i_vesa.h"

#include "sizeopt.h"

#include "options.h"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, and I_StartTic
//
void D_DoomLoop(void);

char *wadfiles[MAXWADFILES];

boolean nomonsters;  // checkparm of -nomonsters
boolean respawnparm; // checkparm of -respawn
boolean fastparm;    // checkparm of -fast

boolean flatSurfaces;
boolean untexturedSurfaces;
boolean flatSky;
boolean flatShadows;
boolean saturnShadows;
boolean showFPS;
boolean unlimitedRAM;
boolean nearSprites;
boolean monoSound;
boolean lowSound;
boolean noMelt;

boolean reverseStereo;

boolean forceHighDetail;
boolean forceLowDetail;
boolean forcePotatoDetail;
int forceScreenSize;

#if defined(MODE_T8025) || defined(MODE_T4025) || defined(MODE_T4050)
boolean CGAcard;
#endif

#if defined(MODE_CGA)
boolean CGApalette1;
#endif

#if defined(MODE_CGA16) || defined(MODE_CGA136) || defined(MODE_CGA_AFH)
boolean snowfix;
#endif

#ifdef SUPPORTS_HERCULES_AUTOMAP
boolean HERCmap;
#endif

#if defined(MODE_Y) || defined(MODE_13H) || defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT) || defined(MODE_V2)
boolean VGADACfix = false;
#endif

#if defined(MODE_T8050) || defined(MODE_T80100)
boolean videoPageFix;
#endif

boolean logTimedemo;
boolean disableDemo;

boolean uncappedFPS;
boolean waitVsync;
boolean simpleStatusBar;
boolean debugPort;

boolean singletics = false; // debug flag to cancel adaptiveness

extern int sfxVolume;
extern int musicVolume;

extern byte inhelpscreens;

unsigned char complevel = 0;

skill_t startskill;
int startepisode;
int startmap;
byte autostart;

byte advancedemo;

boolean modifiedgame;

boolean bfgedition;
gamemode_t gamemode = indetermined;
gamemission_t gamemission = doom;

char basedefault[13] = "fdoom.cfg"; // default file
char sbkfile[13] = "SYNTHGS.SBK";
char iwadfile[13];

void D_CheckNetGame(void);
void D_ProcessEvents(void);
void G_BuildTiccmd(ticcmd_t *cmd);
void D_DoAdvanceDemo(void);

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//
event_t events[MAXEVENTS];
int eventhead;
int eventtail;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(event_t *ev)
{
    events[eventhead] = *ev;
    eventhead = (++eventhead) & (MAXEVENTS - 1);
}

event_t *current_ev;

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents(void)
{
    for (; eventtail != eventhead; eventtail = (++eventtail) & (MAXEVENTS - 1))
    {
        current_ev = &events[eventtail];
        if (M_Responder())
            continue; // menu ate the event
        G_Responder();
    }
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

// wipegamestate can be set to -1 to force a wipe on the next draw
gamestate_t wipegamestate = GS_DEMOSCREEN;
extern byte setsizeneeded;
extern int showMessages;

void R_ExecuteSetViewSize(void);

void D_Display(void)
{
    static byte viewactivestate = 0;
    static byte menuactivestate = 0;
    static byte inhelpscreensstate = 0;
    static byte fullscreen = 0;
    static gamestate_t oldgamestate = -1;
    static int borderdrawcount;
    int tics;
    int wipestart;
    int y;
    boolean done;
    boolean wipe;
    boolean redrawsbar;

    // change the view size if needed
    if (setsizeneeded)
    {
        R_ExecuteSetViewSize();
        oldgamestate = -1; // force background redraw
        borderdrawcount = 3;
    }

    // save the current screen if about to wipe
    if (gamestate != wipegamestate && !noMelt)
    {
        wipe = true;
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        wipe_StartScreen();
#endif
    }
    else
        wipe = false;

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    if (gamestate == GS_LEVEL && gametic)
        HU_Erase();
#endif

    // do buffered drawing
    switch (gamestate)
    {
    case GS_LEVEL:
        if (!gametic)
            break;
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        if (automapactive)
        {
            // [crispy] update automap while playing
            R_RenderPlayerView();
            AM_Drawer();
#if defined(USE_BACKBUFFER)
            updatestate |= I_FULLVIEW;
#endif
        }
#endif

#if defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T8043) || defined(MODE_T8086) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_MDA)
        ST_doPaletteStuff();
#endif
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        if (!automapactive || (automapactive && !fullscreen))
        {
            redrawsbar = wipe || (viewheight != 200 && fullscreen) || (inhelpscreensstate && !inhelpscreens); // just put away the help screen
            ST_Drawer(screenblocks, redrawsbar);
        }
#endif

        fullscreen = viewheight == 200;
        break;

    case GS_INTERMISSION:
        WI_Drawer();
#if defined(USE_BACKBUFFER)
        updatestate |= I_FULLSCRN;
#endif
        break;

    case GS_FINALE:
        F_Drawer();
#if defined(USE_BACKBUFFER)
        updatestate |= I_FULLSCRN;
#endif
        break;

    case GS_DEMOSCREEN:
        D_PageDrawer();
#if defined(USE_BACKBUFFER)
        updatestate |= I_FULLSCRN;
#endif
        break;
    }

    // draw buffered stuff to screen
#if defined(MODE_Y) || defined(MODE_VBE2_DIRECT)
    I_UpdateNoBlit();
#endif

    // draw the view directly

    if (gamestate == GS_LEVEL && gametic)
    {
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        if (!automapactive)
#endif
            R_RenderPlayerView();

        HU_Drawer();
#if defined(USE_BACKBUFFER)
        updatestate |= I_FULLVIEW;
#endif
    }

    // clean up border stuff
    if (gamestate != oldgamestate && gamestate != GS_LEVEL)
    {
        I_SetPalette(0);
    }

    // see if the border needs to be initially drawn
    if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL)
    {
        viewactivestate = 0; // view was not active
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        R_FillBackScreen(); // draw the pattern into the back screen
#endif
    }

    // see if the border needs to be updated to the screen
    if (gamestate == GS_LEVEL
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        && !automapactive
#endif
        && scaledviewwidth != 320)
    {
        if (menuactive || menuactivestate || !viewactivestate)
            borderdrawcount = 3;
        if (borderdrawcount)
        {
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
            R_DrawViewBorder(); // erase old menu stuff
#endif
#if defined(USE_BACKBUFFER)
            updatestate |= I_FULLSCRN;
#endif
            borderdrawcount--;
        }
    }

    menuactivestate = menuactive;
    viewactivestate = viewactive;
    inhelpscreensstate = inhelpscreens;
    oldgamestate = wipegamestate = gamestate;

    // draw pause pic
    if (paused)
    {
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        if (automapactive)
            y = 4;
        else
#endif
            y = viewwindowy + 4;

#if defined(MODE_T4025) || defined(MODE_T4050)
        V_WriteTextDirect(viewwidth / 2 - 2, viewheight / 2, "PAUSE");
#endif

#if defined(MODE_T8025) || defined(MODE_MDA)
        V_WriteTextDirect(viewwidth / 2 - 2, viewheight / 4, "PAUSE");
#endif

#if defined(MODE_T8050) || defined(MODE_T80100) || defined(MODE_T8043) || defined(MODE_T8086)
        V_WriteTextDirect(viewwidth / 2 - 2, viewheight / 2, "PAUSE");
#endif

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
        V_DrawPatchDirect(viewwindowx + (scaledviewwidth - 68) / 2, y, W_CacheLumpName("M_PAUSE", PU_CACHE));
#endif
    }

    // menus go directly to the screen
    M_Drawer(); // menu is drawn even on top of everything

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    if (screenblocks == 11 && gamestate == GS_LEVEL)
    {
        ST_DrawerMini();
    }
#endif

#if defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T8043) || defined(MODE_T8086) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_MDA)
    if (screenblocks <= 11 && gamestate == GS_LEVEL)
    {
#if defined(MODE_T4025) || defined(MODE_T4050)
        ST_DrawerText4025();
#endif
#if defined(MODE_T8025) || defined(MODE_MDA)
        ST_DrawerText8025();
#endif
#if defined(MODE_T8043) || defined(MODE_T8086)
        ST_DrawerText8043();
#endif
#if defined(MODE_T8050) || defined(MODE_T80100)
        ST_DrawerText8050();
#endif
    }
#endif

    NetUpdate(); // send out any new accumulation

    // normal update
    if (!wipe)
    {
        I_FinishUpdate(); // page flip or blit buffer
        return;
    }

// wipe update
#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    wipe_EndScreen();
#endif

    wipestart = ticcount - 1;

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    do
    {
        do
        {
            tics = ticcount - wipestart;
        } while (!tics);
        wipestart = ticcount;
        done = wipe_ScreenWipe(tics);
#if defined(MODE_Y) || defined(MODE_VBE2_DIRECT)
        I_UpdateNoBlit();
#endif
        M_Drawer(); // menu is drawn even on top of wipes
#if defined(USE_BACKBUFFER)
        updatestate = I_FULLSCRN;
#endif
        I_FinishUpdate(); // page flip or blit buffer
    } while (!done);
#endif
}

//
//  D_DoomLoop
//
extern byte demorecording;

void D_DoomLoop(void)
{
    if (demorecording)
        G_BeginRecording();

    I_InitGraphics();

    while (1)
    {
        // process one or more tics
        if (singletics)
        {
            I_StartTic();
            D_ProcessEvents();
            G_BuildTiccmd(&localcmds[maketic & (BACKUPTICS - 1)]);
            if (advancedemo)
                D_DoAdvanceDemo();
            M_Ticker();
            G_Ticker();
            gametic++;
            maketic++;
        }
        else
        {
            TryRunTics(); // will run at least one tic
        }

        S_UpdateSounds(); // move positional sounds

        // Update display, next frame, with current state.
        D_Display();
    }
}

//
//  DEMO LOOP
//
int demosequence;
int pagetic;
char *pagename;

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
    if (--pagetic < 0)
        D_AdvanceDemo();
}

//
// D_PageDrawer
//
void D_PageDrawer(void)
{
#if defined(MODE_T4050)
    V_DrawPatchDirectText4050(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T4025)
    V_DrawPatchDirectText4025(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T8025)
    V_DrawPatchDirectText8025(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_MDA)
    V_DrawPatchDirectTextMDA(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T8043)
    V_DrawPatchDirectText8043(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T8086)
    V_DrawPatchDirectText8086(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T8050)
    V_DrawPatchDirectText8050(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_T80100)
    V_DrawPatchDirectText80100(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(MODE_Y) || defined(MODE_VBE2_DIRECT)
    V_DrawPatchScreen0(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
#if defined(USE_BACKBUFFER)
    V_DrawPatchDirect(0, 0, W_CacheLumpName(pagename, PU_CACHE));
#endif
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
    advancedemo = 1;
}

//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
void D_DoAdvanceDemo(void)
{
    players.playerstate = PST_LIVE; // not reborn
    advancedemo = 0;
    usergame = 0; // no save / end game here
    paused = 0;
    gameaction = ga_nothing;

    if (gamemode == retail)
    {
        demosequence = (demosequence + 1) % 7;
    }
    else
    {
        demosequence = (demosequence + 1) % 6;
    }

    switch (demosequence)
    {
    case 0:
        if (gamemode == commercial)
            pagetic = 35 * 11;
        else
            pagetic = 170;
        gamestate = GS_DEMOSCREEN;

        if (bfgedition)
        {
            pagename = "DMENUPIC";
        }
        else
        {
            pagename = "TITLEPIC";
        }

        if (gamemode == commercial)
            S_ChangeMusic(mus_dm2ttl, false);
        else
            S_ChangeMusic(mus_intro, false);
        break;
    case 1:
        G_DeferedPlayDemo("demo1");
        break;
    case 2:
        pagetic = 200;
        gamestate = GS_DEMOSCREEN;
        pagename = "CREDIT";
        break;
    case 3:
        G_DeferedPlayDemo("demo2");
        break;
    case 4:
        gamestate = GS_DEMOSCREEN;
        if (gamemode == commercial)
        {
            pagetic = 35 * 11;

            if (bfgedition)
            {
                pagename = "DMENUPIC";
            }
            else
            {
                pagename = "TITLEPIC";
            }

            S_ChangeMusic(mus_dm2ttl, false);
        }
        else
        {
            pagetic = 200;

            if (gamemode == retail)
            {
                pagename = "CREDIT";
            }
            else
            {
                pagename = "HELP2";
            }
        }
        break;
    case 5:
        G_DeferedPlayDemo("demo3");
        break;
    case 6:
        G_DeferedPlayDemo("demo4");
        break;
    }
}

//
// D_StartTitle
//
void D_StartTitle(void)
{
    gameaction = ga_nothing;
    demosequence = -1;
    D_AdvanceDemo();
}

//
// D_GetCursorColumn
//
int D_GetCursorColumn(void)
{
    union REGS regs;

    regs.h.ah = 3;
    regs.h.bh = 0;
    int386(0x10, &regs, &regs);

    return regs.h.dl;
}

//
// D_GetCursorRow
//
int D_GetCursorRow(void)
{
    union REGS regs;

    regs.h.ah = 3;
    regs.h.bh = 0;
    int386(0x10, &regs, &regs);

    return regs.h.dh;
}

//
// D_SetCursorPosition
//
void D_SetCursorPosition(int column, int row)
{
    union REGS regs;

    regs.h.dh = row;
    regs.h.dl = column;
    regs.h.ah = 2;
    regs.h.bh = 0;
    int386(0x10, &regs, &regs);
}

//
// D_DrawTitle
//
void D_DrawTitle(char *string, int fc, int bc)
{
    union REGS regs;
    byte color;
    int column;
    int row;
    int i;

    // Calculate text color
    color = (bc << 4) | fc;

    // Get column position
    column = D_GetCursorColumn();

    // Get row position
    row = D_GetCursorRow();

    for (i = 0; i < strlen(string); i++)
    {
        // Set character
        regs.h.ah = 9;
        regs.h.al = string[i];
        regs.w.cx = 1;
        regs.h.bl = color;
        regs.h.bh = 0;
        int386(0x10, &regs, &regs);

        // Check cursor position
        if (++column > 79)
            column = 0;

        // Set position
        D_SetCursorPosition(column, row);
    }
}

//      print title for every printed line
char title[128];

//
// D_RedrawTitle
//
void D_RedrawTitle(void)
{
    int column;
    int row;

    // Get current cursor pos
    column = D_GetCursorColumn();
    row = D_GetCursorRow();

    // Set cursor pos to zero
    D_SetCursorPosition(0, 0);

    // Draw title
    if (complevel >= COMPLEVEL_ULTIMATE_DOOM)
        D_DrawTitle(title, 8, 7);
    else
        D_DrawTitle(title, 4, 7);

    // Restore old cursor pos
    D_SetCursorPosition(column, row);
}

//
// D_AddFile
//
void D_AddFile(char *file)
{
    int numwadfiles;
    char *newfile;

    for (numwadfiles = 0; wadfiles[numwadfiles]; numwadfiles++)
        ;

    newfile = malloc(strlen(file) + 1);
    strcpy(newfile, file);

    wadfiles[numwadfiles] = newfile;
}

void LoadIWAD(int selection)
{
    switch (selection)
    {
    case 49:
        if (!access("doom1.wad", R_OK))
        {
            complevel = COMPLEVEL_DOOM;
            gamemode = shareware;
            gamemission = doom;
            D_AddFile("doom1.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 50:
        if (!access("doom.wad", R_OK))
        {
            complevel = COMPLEVEL_DOOM;
            gamemode = registered;
            gamemission = doom;
            D_AddFile("doom.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 51:
        if (!access("doomu.wad", R_OK))
        {
            complevel = COMPLEVEL_ULTIMATE_DOOM;
            gamemode = retail;
            gamemission = doom;
            D_AddFile("doomu.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 52:
        if (!access("doom2.wad", R_OK))
        {
            complevel = COMPLEVEL_DOOM;
            gamemode = commercial;
            gamemission = doom2;
            D_AddFile("doom2.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 53:
        if (!access("plutonia.wad", R_OK))
        {
            complevel = COMPLEVEL_FINAL_DOOM;
            gamemode = commercial;
            gamemission = pack_plut;
            D_AddFile("plutonia.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 54:
        if (!access("tnt.wad", R_OK))
        {
            complevel = COMPLEVEL_FINAL_DOOM;
            gamemode = commercial;
            gamemission = pack_tnt;
            D_AddFile("tnt.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 55:
        if (!access("freedm1.wad", R_OK))
        {
            complevel = COMPLEVEL_ULTIMATE_DOOM;
            gamemode = retail;
            gamemission = doom;
            D_AddFile("freedm1.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    case 56:
        if (!access("freedm2.wad", R_OK))
        {
            complevel = COMPLEVEL_DOOM;
            gamemode = commercial;
            gamemission = doom2;
            D_AddFile("freedm2.wad");
            return;
        }
        else
        {
            I_Error("The selected IWAD was not found");
        }
        break;
    default:
        I_Error("The selected IWAD was not found");
        break;
    }
}

void LoadExternalIWAD(void)
{
    if (access(iwadfile, R_OK))
        I_Error("The selected IWAD was not found");

    if (!strcmp(iwadfile, "doom1.wad"))
    {
        LoadIWAD(49);
        return;
    }

    if (!strcmp(iwadfile, "doom.wad"))
    {
        LoadIWAD(50);
        return;
    }

    if (!strcmp(iwadfile, "doomu.wad"))
    {
        LoadIWAD(51);
        return;
    }

    if (!strcmp(iwadfile, "doom2.wad"))
    {
        LoadIWAD(52);
        return;
    }

    if (!strcmp(iwadfile, "plutonia.wad"))
    {
        LoadIWAD(53);
        return;
    }

    if (!strcmp(iwadfile, "tnt.wad"))
    {
        LoadIWAD(54);
        return;
    }

    if (!strcmp(iwadfile, "freedm1.wad"))
    {
        LoadIWAD(55);
        return;
    }

    if (!strcmp(iwadfile, "freedm2.wad"))
    {
        LoadIWAD(56);
        return;
    }

    // Unknown IWAD. Load as Doom 2.
    complevel = COMPLEVEL_DOOM;
    gamemode = commercial;
    gamemission = doom2;
    D_AddFile(iwadfile);
    return;
}

//
// IdentifyVersion
// Checks availability of IWAD files by name,
// to determine whether registered/commercial features
// should be executed (notably loading PWAD's).
//
void IdentifyVersion(void)
{
    int selection = -1;
    int num_wads = 0;

    if (iwadfile[0] != 0)
    {
        LoadExternalIWAD();
        return;
    }

    printf("\nFastDoom version " FDOOMVERSION "\n");
    printf("\nPlease select the IWAD you want to play:\n\n");

    if (!access("doom1.wad", R_OK))
    {
        printf("     1. DOOM Shareware                 (doom1.wad)\n");
        selection = 49;
        num_wads++;
    }

    if (!access("doom.wad", R_OK))
    {
        printf("     2. DOOM                           (doom.wad)\n");
        selection = 50;
        num_wads++;
    }

    if (!access("doomu.wad", R_OK))
    {
        printf("     3. The Ultimate DOOM              (doomu.wad)\n");
        selection = 51;
        num_wads++;
    }

    if (!access("doom2.wad", R_OK))
    {
        printf("     4. DOOM II                        (doom2.wad)\n");
        selection = 52;
        num_wads++;
    }

    if (!access("plutonia.wad", R_OK))
    {
        printf("     5. DOOM II: Plutonia Experiment   (plutonia.wad)\n");
        selection = 53;
        num_wads++;
    }

    if (!access("tnt.wad", R_OK))
    {
        printf("     6. DOOM II: TNT - Evilution       (tnt.wad)\n");
        selection = 54;
        num_wads++;
    }

    if (!access("freedm1.wad", R_OK))
    {
        printf("     7. FreeDoom Phase 1               (experimental, freedm1.wad)\n");
        selection = 55;
        num_wads++;
    }

    if (!access("freedm2.wad", R_OK))
    {
        printf("     8. FreeDoom Phase 2               (experimental, freedm2.wad)\n");
        selection = 56;
        num_wads++;
    }

    if (num_wads > 1)
    {
        printf("\nPlease enter the selection: ");
        fflush(stdout);
        selection = getch();
    }

    LoadIWAD(selection);
}

//
// D_DoomMain
//
void D_DoomMain(void)
{
    int p;
    char file[256];
    union REGS regs;

    p = M_CheckParm("-iwad");
    if (p)
    {
        memset(iwadfile, 0, sizeof(iwadfile));
        sprintf(iwadfile, "%s", myargv[p + 1]);
    }

    IdentifyVersion();
    if ((p = M_CheckParm("-complevel")))
    {
        if (p < myargc - 1)
            complevel = atoi(myargv[p + 1]);
    }

#if defined(MODE_PCP) || defined(MODE_CGA16) || defined(MODE_EGA16) || defined(MODE_VGA16) || defined(MODE_EGA80) || defined(MODE_EGAW1) || defined(MODE_EGA) || defined(MODE_CGA_AFH)
    D_AddFile("mode16.wad");
#endif

#if defined(MODE_CGA136) || defined(MODE_VGA136) || defined(MODE_EGA136)
    D_AddFile("mode136.wad");
#endif

#if defined(MODE_CVB)
    D_AddFile("modecvbs.wad");
#endif

#if defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T8043) || defined(MODE_T8086) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100)
    D_AddFile("modetxt.wad");
#endif

#if defined(MODE_MDA)
    D_AddFile("modemda.wad");
#endif

#if defined(MODE_CGA_BW) || defined(MODE_HERC)
    D_AddFile("modebw.wad");
#endif

#if defined(MODE_CGA)
    D_AddFile("mode4.wad");
#endif

#if defined(MODE_ATI640) || defined(MODE_EGA640)
    D_AddFile("mode16d.wad");
#endif

    setbuf(stdout, NULL);
    modifiedgame = false;

    nomonsters = M_CheckParm("-nomonsters");
    respawnparm = M_CheckParm("-respawn");
    fastparm = M_CheckParm("-fast");

    forceHighDetail = M_CheckParm("-forceHQ");
    forceLowDetail = M_CheckParm("-forceLQ");
    forcePotatoDetail = M_CheckParm("-forcePQ");

#if defined(MODE_T8025) || defined(MODE_T4025) || defined(MODE_T4050)
    CGAcard = M_CheckParm("-cga");
#endif

#if defined(MODE_CGA)
    CGApalette1 = M_CheckParm("-palette1");
#endif

#if defined(MODE_CGA16) || defined(MODE_CGA136) || defined(MODE_CGA_AFH)
    snowfix = M_CheckParm("-snow");
#endif

#ifdef SUPPORTS_HERCULES_AUTOMAP
    HERCmap = M_CheckParm("-hercmap");
#endif

#if defined(MODE_Y) || defined(MODE_13H) || defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT) || defined(MODE_V2)
    VGADACfix = M_CheckParm("-fixDAC");
#endif

#if defined(MODE_T8050) || defined(MODE_T80100)
    videoPageFix = M_CheckParm("-pagefix");
#endif

    lowSound = M_CheckParm("-lowsound");

    unlimitedRAM = M_CheckParm("-ram");

    singletics = M_CheckParm("-singletics");

    reverseStereo = M_CheckParm("-reverseStereo");

    logTimedemo = M_CheckParm("-logTimedemo");

    disableDemo = M_CheckParm("-disabledemo");

    bfgedition = M_CheckParm("-bfg");

    if ((p = M_CheckParm("-size")))
    {
        if (p < myargc - 1)
            forceScreenSize = atoi(myargv[p + 1]);
        if (forceScreenSize < 3)
            forceScreenSize = 3;
        else if (forceScreenSize > 12)
            forceScreenSize = 12;
    }

    p = M_CheckParm("-sbk");
    if (p)
    {
        memset(sbkfile, 0, sizeof(sbkfile));
        sprintf(sbkfile, "%s", myargv[p + 1]);
    }

    switch (gamemode)
    {
    case retail:
        sprintf(title,
                "                         "
                "The Ultimate DOOM Startup v%i.%i"
                "                        ",
                VERSION / 100, VERSION % 100);
        break;
    case shareware:
        sprintf(title,
                "                            "
                "DOOM Shareware Startup v%i.%i"
                "                           ",
                VERSION / 100, VERSION % 100);
        break;
    case registered:
        sprintf(title,
                "                          "
                "DOOM System Startup v%i.%i"
                "                          ",
                VERSION / 100, VERSION % 100);
        break;
    case commercial:
        switch (gamemission)
        {
        case doom2:
            sprintf(title,
                    "                         "
                    "DOOM 2: Hell on Earth v%i.%i"
                    "                           ",
                    VERSION / 100, VERSION % 100);
            break;
        case pack_plut:
            sprintf(title,
                    "                   "
                    "DOOM 2: Plutonia Experiment v%i.%i"
                    "                           ",
                    VERSION / 100, VERSION % 100);
            break;
        case pack_tnt:
            sprintf(title,
                    "                     "
                    "DOOM 2: TNT - Evilution v%i.%i"
                    "                           ",
                    VERSION / 100, VERSION % 100);
            break;
        }
        break;
    }

    regs.w.ax = 3;
    int386(0x10, &regs, &regs);
    if (complevel >= COMPLEVEL_ULTIMATE_DOOM)
        D_DrawTitle(title, 8, 7);
    else
        D_DrawTitle(title, 4, 7);

    printf("\nFastDoom version " FDOOMVERSION "\n");
    printf("P_Init: Checking cmd-line parameters...\n");

    // turbo option
    if ((p = M_CheckParm("-turbo")))
    {
        int scale = 200;
        extern int forwardmove[2];
        extern int sidemove[2];

        if (p < myargc - 1)
            scale = atoi(myargv[p + 1]);
        if (scale < 10)
            scale = 10;
        else if (scale > 400)
            scale = 400;
        printf("turbo scale: %i%%\n", scale);
        forwardmove[0] = forwardmove[0] * scale / 100;
        forwardmove[1] = forwardmove[1] * scale / 100;
        sidemove[0] = sidemove[0] * scale / 100;
        sidemove[1] = sidemove[1] * scale / 100;
    }

    p = M_CheckParm("-file");
    if (p)
    {
        // the parms after p are wadfile/lump names,
        // until end of parms or another - preceded parm
        modifiedgame = true; // homebrew levels
        while (++p != myargc && myargv[p][0] != '-')
            D_AddFile(myargv[p]);
    }

    p = M_CheckParm("-playdemo");

    if (!p)
        p = M_CheckParm("-timedemo");

    if (p && p < myargc - 1)
    {
        sprintf(file, "%s.lmp", myargv[p + 1]);
        D_AddFile(file);
        printf("Playing demo %s.lmp.\n", myargv[p + 1]);
    }

    // get skill / episode / map from parms
    startskill = sk_medium;
    startepisode = 1;
    startmap = 1;
    autostart = 0;

    p = M_CheckParm("-skill");
    if (p && p < myargc - 1)
    {
        startskill = myargv[p + 1][0] - '1';
        autostart = 1;
    }

    p = M_CheckParm("-episode");
    if (p && p < myargc - 1)
    {
        startepisode = myargv[p + 1][0] - '0';
        startmap = 1;
        autostart = 1;
    }

    p = M_CheckParm("-warp");
    if (p && p < myargc - 1)
    {
        if (gamemode == commercial)
            startmap = atoi(myargv[p + 1]);
        else
        {
            startepisode = myargv[p + 1][0] - '0';
            startmap = myargv[p + 2][0] - '0';
        }
        autostart = 1;
    }

    printf("M_LoadDefaults: Load system defaults.\n");
    M_LoadDefaults(); // load before initing other systems

    M_CheckParmOptional("-fps", &showFPS);

    if (M_CheckParmOptional("-flattersurfaces", &flatSurfaces) && untexturedSurfaces)
    {
        untexturedSurfaces = 0;
    }
    if (M_CheckParmOptional("-flatsurfaces", &untexturedSurfaces) && flatSurfaces)
    {
        flatSurfaces = 0;
    }

    M_CheckParmOptional("-flatsky", &flatSky);

    if (M_CheckParmOptional("-flatshadows", &flatShadows) && saturnShadows)
    {
        saturnShadows = 0;
    }

    if (M_CheckParmOptional("-saturn", &saturnShadows) && flatShadows)
    {
        flatShadows = 0;
    }
    M_CheckParmOptional("-mono", &monoSound);
    M_CheckParmOptional("-near", &nearSprites);
    M_CheckParmOptional("-nomelt", &noMelt);
    M_CheckParmOptional("-uncapped", &uncappedFPS);
    M_CheckParmOptional("-vsync", &waitVsync);
    M_CheckParmOptional("-simplestatusbar", &simpleStatusBar);
    M_CheckParmOptional("-debugPort", &debugPort);
    M_CheckParmDisable("-normalsurfaces", &flatSurfaces);
    M_CheckParmDisable("-normalsurfaces", &untexturedSurfaces);
    M_CheckParmDisable("-normalsky", &flatSky);
    M_CheckParmDisable("-normalshadows", &flatShadows);
    M_CheckParmDisable("-normalshadows", &saturnShadows);
    M_CheckParmDisable("-normalsprites", &nearSprites);
    M_CheckParmDisable("-normalstatusbar", &simpleStatusBar);
    M_CheckParmDisable("-stereo", &monoSound);
    M_CheckParmDisable("-melt", &noMelt);
    M_CheckParmDisable("-capped", &uncappedFPS);
    M_CheckParmDisable("-novsync", &waitVsync);
    M_CheckParmDisable("-nofps", &showFPS);

#if defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T8043) || defined(MODE_T8086) || defined(MODE_T80100) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_MDA)
    noMelt = 1;
#endif

    printf("Z_Init: Init zone memory allocation daemon. \n");
    Z_Init();

    printf("W_Init: Init WADfiles.\n");
    W_InitMultipleFiles(wadfiles);

    printf("M_Init: Init miscellaneous info.\n");
    D_RedrawTitle();
    M_Init();

    printf("R_Init: Init DOOM refresh daemon - ");
    D_RedrawTitle();
    R_Init();

    printf("\nP_Init: Init Playloop state.\n");
    D_RedrawTitle();
    P_Init();

    printf("I_Init: Setting up machine state.\n");
    D_RedrawTitle();
    I_Init();

    printf("D_CheckNetGame: Checking network game status.\n");
    D_RedrawTitle();
    D_CheckNetGame();

    printf("S_Init: Setting up sound.\n");
    D_RedrawTitle();
    S_Init(sfxVolume * 8, musicVolume * 17);

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    printf("HU_Init: Setting up heads up display.\n");
    D_RedrawTitle();
    HU_Init();
#endif

#if defined(MODE_Y) || defined(USE_BACKBUFFER) || defined(MODE_VBE2_DIRECT)
    printf("ST_Init: Init status bar.\n");
    D_RedrawTitle();
    ST_Init();
#endif

    // start the apropriate game based on parms
    p = M_CheckParm("-record");

    if (p && p < myargc - 1)
    {
        G_RecordDemo(myargv[p + 1]);
        autostart = 1;
    }

    p = M_CheckParm("-playdemo");
    if (p && p < myargc - 1)
    {
        singledemo = 1; // quit after one demo
        G_DeferedPlayDemo(myargv[p + 1]);
        D_DoomLoop(); // never returns
    }

    p = M_CheckParm("-timedemo");
    if (p && p < myargc - 1)
    {
        G_TimeDemo(myargv[p + 1]);
        D_DoomLoop(); // never returns
    }

    p = M_CheckParm("-loadgame");
    if (p && p < myargc - 1)
    {
        sprintf(file, SAVEGAMENAME "%c.dsg", myargv[p + 1][0]);
        G_LoadGame(file);
    }

    if (gameaction != ga_loadgame)
    {
        if (autostart)
            G_InitNew(startskill, startepisode, startmap);
        else
            D_StartTitle(); // start up intro loop
    }

    D_DoomLoop(); // never returns
}
