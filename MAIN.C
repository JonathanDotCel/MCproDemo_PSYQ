// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdlib.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <libetc.h>

// Function definittions in the submodule
#include "cyblib/mcpro.h"

//
// Lang Defines
//
#define ulong unsigned long
#define bool int
#define true 1
#define false 0

//
// HW Defines
//
#define ISTAT 0xBF801070
#define pISTAT *(volatile ulong *)ISTAT

#define IMASK 0xBF801074
#define pIMASK *(volatile ulong *)IMASK

//
// Env Defines
//
#define SCREEN_WIDTH 512
#define SCREEN_HEIGHT 240

//
// PsyQ Defines
//
#define OTLEN 16
#define BUFFSIZE 256
#define TIMEOUT 999999

//
// Vars
//
static int activeBuffer;
static long ot[2][OTLEN];
static DISPENV disp[2];
static DRAWENV draw[2];

// current and last pad vals to detect
// a button a press (vs holding it)
static int lastPadVals = 0;
static int padVals = 0;
// IRQ mask before doing pad stuff
static ulong wasInCritical = 0;
static ulong lastIRQMask = 0;

void UpdatePads()
{
    lastPadVals = padVals;
    padVals = PadRead(0);
}

// Button released on pad
int Released(ulong inButton)
{

    char returnVal = (!(padVals & inButton) && (lastPadVals & inButton));

    // pad's not ready or something's wrong
    if (padVals == 0xFFFFFFFF)
        return 0;

    // Clear this event
    if (returnVal)
    {
        lastPadVals ^= inButton;
    }

    return returnVal;
}

// Clear the display list, wait for a vsync and flip buffers
void StartDraw()
{

    ClearOTag(ot[activeBuffer], OTLEN);

    VSync(0);

    activeBuffer ^= 1;
}

// Draw it
void EndDraw()
{

    PutDispEnv(&disp[activeBuffer]);
    PutDrawEnv(&draw[activeBuffer]);

    DrawOTag(ot[activeBuffer ^ 1]);
    FntFlush(-1);
}

// Little message box with "Press X to continue"
void QuickMessage(char *message, int param0, int param1)
{

    while (!Released(PADRdown) && !Released(PADstart))
    {
        StartDraw();
        FntPrint(message, param0, param1);
        FntPrint("\n\n Press X to continue");
        UpdatePads();
        EndDraw();
    }
}

// About a second
void Delay()
{

    int i;

    for (i = 0; i < 7000000; i++)
    {
        __asm__("nop\n");
    }
}

// Get int enable state from cop0r12
// e.g. so we can detect nested criticals
bool InCriticalSection()
{
    ulong returnVal;
    __asm__ volatile(
        "mfc0 %0,$12\n\t"
        "nop\n\t"
        : "=r"(returnVal)
        : // no inputs
    );

    return !(returnVal & 0x01);
}

// Enter a critical section by disabling interrupts
bool EnterCritical()
{
    ulong oldVal = InCriticalSection();

    ulong tmp0, tmp1;

    __asm__ volatile(
        "li   %1, 0x1\n\t"  // li tmp1,0x01
        "not  %1\n\t"       // not tmp1
        "mfc0 %0, $12\n\t"  // mfc0 tmp0,$12
        "nop  \n\t"         // best opcode
        "and  %0,%0,%1\n\t" // tmp0 = tmp0 & tmp1 (mask it)
        "mtc0 %0, $12\n\t"  // send it back
        "nop"
        : "=r"(tmp0), "=r"(tmp1)
        : // no inputs
    );

    return oldVal;
}

// Exit critical by re-enabling interrupts
void ExitCritical()
{
    ulong tmp0, tmp1;

    __asm__ volatile(
        "mfc0 %0, $12\n\t"
        "nop  \n\t"
        "ori  %0,%0,0x01\n\t"
        //"ori  %0,%0,0x10\n\t"
        "ori  %0,%0,0xFF00\n\t" // allow all the int types, master mask
        "mtc0 %0, $12\n\t"
        "nop"
        : "=r"(tmp0), "=r"(tmp1)
        : // no inputs
    );
}

// Call before and after sending to MCPro to give time to hit record
// on the Logic Analyser, without bus noise from other peripherals
void Countdown(bool isPreCounter)
{

    int i;
    int timerStartVal = 3;

    // Finish the current frame and clear the screen
    StartDraw();
    "Countdown....";
    EndDraw();

    // Do this after the
    if (isPreCounter)
    {
        int wasInCritical = 0;

        wasInCritical = EnterCritical();
        lastIRQMask = pIMASK;
    }

    for (i = 3; i > 0; i--)
    {

        StartDraw();
        if (isPreCounter)
        {
            FntPrint("Sending in %d (Start Logic Analsyer now)", i, 0);
        }
        else
        {
            FntPrint("Restoring control in %d (Stop Logic Analyser now)", i, 0);
        }
        EndDraw();

        Delay();
    }

    if (!isPreCounter)
    {
        // If we were already in a critical section, don't re-enable interrupts
        // as we may be nested
        if (!wasInCritical)
        {
            ExitCritical();
            pIMASK = lastIRQMask;
        }
    }
}

// Little wrapper
void SendGameID(int port01, char *gameID)
{

    int returnVal = 0;
    Countdown(true);
    returnVal = MemCardPro_SendGameID(port01, strlen(gameID), gameID);
    Countdown(false);

    QuickMessage("Got return val %d\n", returnVal, 0);
}

void DrawLoop()
{

    padVals = 0;
    lastPadVals = 0;

    printf("Program started...\n");

    while (1)
    {

        StartDraw();

        UpdatePads();

        FntPrint("\n");

        FntPrint("MCPRO/PSIO Test Program\n");
        FntPrint(" \n");

        FntPrint("Key Combos:\n\n");
        FntPrint("    X: Ping Card\n");
        FntPrint("   []: Send cdrom:SCUS_944.55;1\n");
        FntPrint("   /\\: Send SCUS_944.55\n");
        FntPrint("    O: Send VeryLongFileNameTestEndingWith...\n");
        FntPrint("L1/R1: Change channel\n");
        FntPrint("L2/R2: Change game\n");
        FntPrint("start: Reboot\n");
        FntPrint(" \n");

        // prev chan
        if (Released(PADL1))
        {
            int returnVal = 0;
            Countdown(true);
            returnVal = MemCardPro_PrevCH(MCPRO_PORT_0);
            Countdown(false);
        }

        // next chan
        if (Released(PADR1))
        {
            int returnVal = 0;
            Countdown(true);
            returnVal = MemCardPro_NextCH(MCPRO_PORT_0);
            Countdown(false);
        }

        // prev dir
        if (Released(PADL2))
        {
            int returnVal = 0;
            Countdown(true);
            returnVal = MemCardPro_PrevDIR(MCPRO_PORT_0);
            Countdown(false);
        }

        // next dir
        if (Released(PADR2))
        {
            int returnVal = 0;
            Countdown(true);
            returnVal = MemCardPro_NextDIR(MCPRO_PORT_0);
            Countdown(false);
        }

        // reboot
        if (Released(PADstart))
        {
            goto *(ulong *)0xBFC00000;
        }

        // ping
        if (Released(PADRdown))
        {
            int returnVal = 0;

            Countdown(true);
            returnVal = MemCardPro_Ping(MCPRO_PORT_0);
            Countdown(false);

            QuickMessage("Got return val %d\n", returnVal, 0);
        }

        // GameID
        if (Released(PADRleft))
        {
            // Gran Turismo 2
            SendGameID(MCPRO_PORT_0, "cdrom:SCUS_944.55;1");
        }

        // GameID
        if (Released(PADRup))
        {
            // Gran Turismo 2
            SendGameID(MCPRO_PORT_0, "SCUS_944.55");
        }

        // GameID
        if (Released(PADRright))
        {
            SendGameID(MCPRO_PORT_0, "VeryLongFileNameTestEndingWithAPredictableBytePattern010101");
        }

        EndDraw();
    }
}

int main()
{

    // In case we were
    ExitCritical();

    // Standard sony stuff
    SetDispMask(0);
    ResetGraph(0);
    SetGraphDebug(0);
    ResetCallback();
    // ExitCriticalSection();

    SetDefDrawEnv(&draw[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&draw[1], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&disp[0], 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&disp[1], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    draw[0].isbg = draw[1].isbg = 1;

    // Pantone 17-1937 TCX - HOT PINK!
    setRGB0(&draw[0], 0xFF, 0x69, 0xB4);
    setRGB0(&draw[1], 0xFF, 0x69, 0xB4);
    PutDispEnv(&disp[0]);
    PutDrawEnv(&draw[0]);

    // Initialize onscreen font and text output system
    FntLoad(960, 256);
    SetDumpFnt(FntOpen(16, 16, 512 - 32, 200, 0, 700));

    // Wait for a VBlank before turning on the display
    VSync(0);
    SetDispMask(1);

    // Prod the bios for the E/J/U identifier
    {
        char isPAL = *(char *)0xBFC7FF52 == 'E';
        SetVideoMode(isPAL);
    }

    activeBuffer = 0;

    PadInit(0);

    DrawLoop();

    return 0;
}
