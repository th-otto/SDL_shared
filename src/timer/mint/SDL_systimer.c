/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#ifdef SDL_TIMER_MINT

/*
 *	TOS/MiNT timer driver
 *	based on RISCOS backend
 *
 *	Patrice Mandin
 */

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <mint/cookie.h>
#include <mint/sysvars.h>
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

#include "SDL_timer.h"
#include "../SDL_timer_c.h"

#include "../../video/ataricommon/SDL_atarisuper.h"
#include "../../video/ataricommon/SDL_xbiosevents_c.h"
#include "../../video/ataricommon/SDL_ikbdevents_c.h"

#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 200


/* from src/video/ataricommon/SDL_atarievents.c */
void SDL_AtariMint_BackgroundTasks(void);

static volatile clock_t counter_200hz; /* 200 HZ tick when we started the program. */

/* The first ticks value of the application */
static Uint32 start;

/* Timer  SDL_arraysize(Timer ),start/reset time */
static Uint32 timerStart;

/*
 * replace also mintlib function
 */
clock_t clock(void)
{
    return counter_200hz;
}

/* This next bit of nonsense is temporary...clock() should be fixed! */
__typeof__(clock) _clock;

clock_t _clock(void) __attribute__ ((alias ("clock")));

void SDL_StartTicks(void)
{
	/* Set first ticks value, one _hz_200 tic is 5ms */
	start = clock() * (1000 / CLOCKS_PER_SEC);
}

Uint32 SDL_GetTicks (void)
{
	Uint32 now = clock() * (1000 / CLOCKS_PER_SEC);

	return(now-start);
}

static long atari_200hz_init(void)
{
	__asm__ __volatile__(
	"\tmove    %%sr,%%d0\n"
	"\tmove    %%d0,%%d1\n"
#ifdef __mcoldfire__
	"\tor.l    #0x700,%%d1\n"
#else
	"\tor.w    #0x700,%%d1\n"
#endif
	"\tmove    %%d1,%%sr\n"

	"\tlea	   my_200hz-4(%%pc),%%a0\n"
	"\tmove.l  0x114.w,(%%a0)\n"
	"\taddq.l  #4,%%a0\n"
	"\tmove.l  %%a0,0x114.w\n"

	"\tmove    %%d0,%%sr\n"
	"\tjbra 1f\n"

	"\t.dc.l  0x58425241\n" /* "XBRA" */
	"\t.dc.l  0x4c53444c\n" /* "LSDL" */
	"\t.dc.l  0\n"
"my_200hz:\n"
	"\taddq.l  #1,%0\n"

	"\tmove.l  my_200hz-4(%%pc),-(%%sp)\n"
	"\trts\n"
"1:\n"
	: /* output */
	: "m"(counter_200hz) /* inputs */
	: "d0", "d1", "a0", "memory", "cc");
	return 0;
}

static long atari_200hz_shutdown(void)
{
	__asm__ __volatile__(
	"\tmove    %%sr,%%d0\n"
	"\tmove    %%d0,%%d1\n"
#ifdef __mcoldfire__
	"\tor.l    #0x700,%%d1\n"
#else
	"\tor.w    #0x700,%%d1\n"
#endif
	"\tmove    %%d1,%%sr\n"

	"\tlea	   my_200hz-4(%%pc),%%a0\n"
	"\tmove.l  (%%a0),0x114.w\n"

	"\tmove    %%d0,%%sr\n"
	: /* output */
	: /* inputs */
	: "d0", "d1", "a0", "memory", "cc");
	return 0;
}


static void exit_vectors(void)
{
	SDL_timer_running = 0;
	Supexec(atari_200hz_shutdown);
	SDL_AtariXbios_RestoreVectors();
	AtariIkbd_ShutdownEvents();
	Buffoper(0);
	Jdisint(MFP_DMASOUND);
	NSetinterrupt(2, SI_NONE, 0);
	Jdisint(MFP_TIMERA);
	Unlocksnd();
}

# define PGETFLAGS       (('P'<< 8) | 5)

__attribute__((__constructor__))
static void init_clock(void)
{
	int fd;
	long arg;
	unsigned int protmode;

	/*
	 * If PMMU is active, check that our protection mode is sufficent.
	 * Interrupt routines will write to our memory even when other processes
	 * are running.
	 * A protection mode of 2 (Super) or 1 (Global) is sufficient.
	 * A protection mode of 0 (Private) or 3 (readonly) is not.
	 */
	if (Getcookie(C_PMMU, &arg) == C_FOUND)
	{
		fd = Fopen("U:\\proc\\.-1", 0);
		if (fd > 0)
		{
			arg = 0;
			if (Fcntl(fd, &arg, PGETFLAGS) >= 0)
			{
				protmode = arg & 0xf0;
				if (protmode == 0 || protmode >= (3 << 4))
				{
					(void) Cconws("SDL: warning: PMMU is active, change memory-protection\r\n");
				}
			}
			Fclose(fd);
		}
	}
	Supexec(atari_200hz_init);
	Setexc(VEC_PROCTERM, exit_vectors);
}

void SDL_Delay (Uint32 ms)
{
	static Uint32 prev_now = 0;
	Uint32 now, cur_tick;
	int ran_bg_task = 0;

	now = cur_tick = SDL_GetTicks();

	/* No need to loop for delay below resolution */
	if (ms < (1000 / CLOCKS_PER_SEC)) {
		if (prev_now != now) {
			SDL_AtariMint_BackgroundTasks();
			prev_now = now;
		}
		return;
	}

	while (cur_tick-now<ms){
		if (prev_now != cur_tick) {
			SDL_AtariMint_BackgroundTasks();
			prev_now = cur_tick;
			ran_bg_task = 1;
		}
		Syield();
		cur_tick = SDL_GetTicks();
	}

	if (!ran_bg_task) {
		SDL_AtariMint_BackgroundTasks();
	}
}

/* Non-threaded version of timer */

int SDL_SYS_TimerInit(void)
{
	return(0);
}

void SDL_SYS_TimerQuit(void)
{
	SDL_SetTimer(0, NULL);
}

int SDL_SYS_StartTimer(void)
{
	timerStart = SDL_GetTicks();

	return(0);
}

void SDL_SYS_StopTimer(void)
{
	/* Don't need to do anything as we use SDL_timer_running
	   to detect if we need to check the timer */
}


void SDL_AtariMint_CheckTimer(void)
{
	if (SDL_timer_running && SDL_GetTicks() - timerStart >= SDL_alarm_interval)
	{
		Uint32 ms;

		ms = SDL_alarm_callback(SDL_alarm_interval);
		if ( ms != SDL_alarm_interval )
		{
			if ( ms )
			{
				SDL_alarm_interval = ROUND_RESOLUTION(ms);
			} else
			{
				SDL_alarm_interval = 0;
				SDL_timer_running = 0;
			}
		}
		if (SDL_alarm_interval) timerStart = SDL_GetTicks();
	}
}

#define USEC_PER_TICK (1000000L / ((unsigned long)CLOCKS_PER_SEC))
#define	USEC_TO_CLOCK_TICKS(us)	((us) / USEC_PER_TICK )

/*
 * Sleep for usec microSeconds
 * the actual suspension time can be arbitrarily longer
 *
 */
int usleep(__useconds_t __useconds)
{
	long stop;
	int r = -ENOSYS;

	if (__useconds >= 1000)
		r = Fselect((unsigned)(__useconds/1000), 0, 0, 0);

	if (r == -ENOSYS)
	{
		stop = clock() + USEC_TO_CLOCK_TICKS(__useconds);
		while (clock() < stop)
			Syield();
		r = 0;
	}

	if (r < 0)
	{
		errno = -r;
		return -1;
	}

	return 0;
}

#endif /* SDL_TIMER_MINT */
