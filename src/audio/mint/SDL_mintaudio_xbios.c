/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/*
	MiNT audio driver
	using XBIOS functions (Falcon)

	Patrice Mandin, Didier Méquignon

	Falls back to direct hardware access,
	if XBIOS is not available
*/

#include <unistd.h>
#include <support.h>

/* Mint includes */
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>
#include <mint/sysvars.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include "../../video/ataricommon/SDL_atarimxalloc_c.h"
#include "../../video/ataricommon/SDL_atarisuper.h"

#include "SDL_mintaudio.h"
#include "SDL_mintaudio_dma8.h"
#include "SDL_mintaudio_mcsn.h"

#ifndef MODE_MONO8
#define MODE_MONO8		MODE_MONO
#endif

/* Extended modes. Only available with SND_EXT */

#ifndef MODE_MONO16
#define MODE_MONO16		3	/* 16 bit mono, big-endian */
#define MODE_STEREO24	4	/* 24 bit stereo, big-endian */
#define MODE_STEREO32	5	/* 32 bit stereo, big-endian */
#define MODE_MONO24		6	/* 24 bit mono, big-endian */
#define MODE_MONO32		7	/* 32 bit mono, big-endian */
#endif
#ifndef SETSMPFREQ
#define	SETSMPFREQ	7
#endif

/*--- Defines ---*/

#define MINT_AUDIO_DRIVER_NAME "mint_xbios"

/* Debug print info */
#define DEBUG_NAME "audio:xbios: "
#if 0
#define DEBUG_PRINT(what) \
	{ \
		printf what; \
	}
#else
#define DEBUG_PRINT(what)
#endif

/*--- Static variables ---*/

static long cookie_snd;
static int fix_devconnect;
static short oldGpio = -1;
static short oldAdderIn;
static short oldAdcInput;
static short oldPrescale;
static int has8bitStereo;
static int hasFreeFrequency;

/*--- interrupt function ---*/

volatile unsigned long SDL_MintAudio_num_its;
void *SDL_MintAudio_itbuffer;
unsigned long SDL_MintAudio_itbuflen;
unsigned long SDL_MintAudio_itsilence;

/* Xbios, DMA 8 bits: need to set ISR on MFP */

__attribute__((interrupt))
void SDL_MintAudio_XbiosInterrupt(void)
{
	unsigned long num_its;
	unsigned long *buffer;
	long buflen;
	
	/* Clear service bit, so other MFP interrupts can work */
	*((volatile char *)0xfffffa0f) = 0xdf;
	
	num_its = SDL_MintAudio_num_its;
	++num_its;
	SDL_MintAudio_num_its = num_its;
	if (num_its >= 5)
	{
		/* Fill audio buffer with silence when too many interrupts triggered on same buffer */
		buffer = (unsigned long *)SDL_MintAudio_itbuffer;
		if (buffer != NULL)
		{
			buflen = SDL_MintAudio_itbuflen;
			if (buflen > 0)
			{
				do
				{
					*buffer++ = SDL_MintAudio_itsilence;
				} while (--buflen >= 0);
			}
		}
	}
}

/*--- Audio driver functions ---*/

static long get_sysbase(void)
{
	return *((long *)0x4f2);
}

static int isEmutos(void)
{
	OSHEADER *sysbase = (OSHEADER *)Supexec(get_sysbase);

	return ((long)sysbase->p_rsv2) == 0x45544f53L; /* "ETOS" */
}

static int Audio_Available(void)
{
	short locked;
	const char *envr = getenv("SDL_AUDIODRIVER");
	long mcsn = 0;

	/* Check if user asked a different audio driver */
	if (envr && strcmp(envr, MINT_AUDIO_DRIVER_NAME) != 0)
	{
		DEBUG_PRINT((DEBUG_NAME "user asked a different audio driver\n"));
		return 0;
	}

	/* Cookie _SND present ? if not, assume ST machine */
	if (Getcookie(C__SND, &cookie_snd) != C_FOUND)
	{
		cookie_snd = 0;
	}

	has8bitStereo = 1;
	hasFreeFrequency = 0;
	if (Getcookie(C_McSn, &mcsn) == C_FOUND && mcsn != 0)
	{
		/* check whether 8-bit stereo is available */
		cookie_mcsn_t *mcsnCookie = (cookie_mcsn_t *) mcsn;

		has8bitStereo = mcsnCookie->play == MCSN_TT || mcsnCookie->play == MCSN_FALCON;	/* STE/TT or Falcon */

		if (mcsnCookie->play == MCSN_FALCON)
		{
			hasFreeFrequency = 1;
		}

		/* X-Sound doesn't set _SND (MacSound does) */
		if (cookie_snd == 0)
			cookie_snd |= SND_PSG | SND_8BIT;
	}
	
	/* Check if we have 16 bits audio */
	if ((cookie_snd & (SND_8BIT | SND_16BIT)) == 0)
	{
		DEBUG_PRINT((DEBUG_NAME "no DMA sound\n"));
		return 0;
	}

	if (cookie_snd & SND_EXT)
	{
		hasFreeFrequency = 1;
	}

	/* Check if audio is lockable */
	locked = (short)Locksnd();
	if (locked != 1 && locked != 0x80) /* XBIOS function not available */
	{
		DEBUG_PRINT((DEBUG_NAME "audio locked by other application\n"));
		return 0;
	}

	Unlocksnd();

	/*
	 * do no attempt to replace Devconnect if a sound driver is installed,
	 * or EmuTOS is used
	 */
	fix_devconnect = 1;
	if (mcsn != 0 ||
		Getcookie(C_GSXB, NULL) == C_FOUND ||
		Getcookie(C_STFA, NULL) == C_FOUND ||
		isEmutos())
		fix_devconnect = 0;

	DEBUG_PRINT((DEBUG_NAME "XBIOS audio available!\n"));
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

/* Falcon XBIOS implementation of Devconnect() is buggy with external clock */

static void ste_setprescale(short data)
{
	if ((short)Soundcmd(SETPRESCALE, data) != data)
	{
		void *oldstack = (void *) Super(0);
		unsigned char modectrl = DMAAUDIO_IO.sound_ctrl & 0xFC;
		modectrl |= data;
		DMAAUDIO_IO.sound_ctrl = modectrl;
		SuperToUser(oldstack);
	}
}

static long ste_devconnect(int src, int dst, int sclk, int pre)
{
	short data;

	if (src != DMAPLAY || dst != DAC || sclk != CLK25M)
		return -5; /* EBADRQ */

	switch (pre)
	{
	case CLKOLD:		/* Set STE compatible mode -> we can abort here */
		return 0;
	case CLK50K:
		data = 3;	/* 50 kHz */
		break;
	case CLK33K:
	case CLK25K:
	case CLK20K:
		data = 2;	/* 25 kHz */
		break;
	case CLK16K:
	case CLK16K + 1:
	case CLK12K:
	case CLK12K + 1:
		data = 1;	/* 12 kHz */
		break;
	default:
		data = 0;			/* leave it at 6 kHz */
		break;
	}

	ste_setprescale(data);
	
    return 0;
}

static long DevconnectWorkaround(int src, int dst, int sclk, int pre, int protocol)
{
	unsigned short dev_ctrl;
	unsigned short dest_ctrl;
	void *oldstack;

	if (dst == 0)
		return 0;
	if (!fix_devconnect)
		return Devconnect(src, dst, sclk, pre, protocol);
	if ((cookie_snd & SND_16BIT) == 0)
		return ste_devconnect(src, dst, sclk, pre);
		
	oldstack = (void *) Super(0);

	dev_ctrl = DMAAUDIO_IO.dev_ctrl;
	dest_ctrl = DMAAUDIO_IO.dest_ctrl;
	sclk &= 3;

	switch (src)
	{
	case DMAPLAY:
		dev_ctrl &= 0xfff0;
		dev_ctrl |= (sclk << 1);
		if (protocol != HANDSHAKE)
		{
			dev_ctrl |= 0x0001;
		} else
		{
			dev_ctrl &= ~0x0001;
			if (dst & DSPRECV)
				dev_ctrl &= ~0x0008;
			else
				dev_ctrl |= 0x0008;
		}
		break;
	case DSPXMIT:
		dev_ctrl &= 0xff8f;
		dev_ctrl |= (sclk << 5);
		if (protocol != HANDSHAKE)
			dev_ctrl |= 0x0010;
		else
			dev_ctrl |= ~0x0010;
		break;
	case EXTINP:
		dev_ctrl &= 0xf0ff;
		dev_ctrl |= (sclk << 9);
		if (protocol != HANDSHAKE)
			dev_ctrl |= 0x0100;
		else
			dev_ctrl |= ~0x0100;
		break;
	case ADC:
		dev_ctrl &= 0x0fff;
		if (sclk & CLKEXT)
			dev_ctrl |= 0x6000;
		break;
	default:
		return 0;
	}

	if (dst & DMAREC)
	{
		dest_ctrl &= 0xFFF0;
		dest_ctrl |= src << 1;
		if (protocol != HANDSHAKE)
		{
			dest_ctrl |= 0x0001;
		} else
		{
			dest_ctrl &= ~0x0001;
			if (src == DSPXMIT)
				dest_ctrl &= ~0x0008;
			else
				dest_ctrl |= 0x0008;
		}
	}

	if (dst & DSPRECV)
	{
		dest_ctrl &= 0xFF8F;
		dest_ctrl |= src << 5;
		if (protocol != HANDSHAKE)
			dest_ctrl |= 0x0010;
		else
			dest_ctrl |= ~0x0010;
	}

	if (dst & EXTOUT)
	{
		dest_ctrl &= 0xF0FF;
		dest_ctrl |= src << 9;
		if (protocol != HANDSHAKE)
			dest_ctrl |= 0x0100;
		else
			dest_ctrl |= ~0x0100;
	}

	if (dst & DAC)
	{
		dest_ctrl &= 0x0FFF;
		if (sclk & CLKEXT)
			dest_ctrl |= 0x6000;
		dest_ctrl &= 0x0FFF;
		dest_ctrl |= (src << 13);
	}

	DMAAUDIO_IO.dev_ctrl = dev_ctrl;
	DMAAUDIO_IO.dest_ctrl = dest_ctrl;
	if (sclk & CLKEXT)
	{
		DMAAUDIO_IO.freq_ext = pre;
	} else
	{
		DMAAUDIO_IO.freq_int = pre;
	}

	SuperToUser(oldstack);
	
	return 1;
}
#undef Devconnect
#define Devconnect DevconnectWorkaround


/*
 * Find out the speed of external clock
 * even for a dual external clock !!!
 * (FDI supported)
 *
 * Copyright STGHOST/SECTOR ONE 1999
 * Modified for SDL use
 */
static long externalClockTest(void)
{
	register int ret __asm__("d0");

	__asm__ volatile(
		"	lea		0xffff8901.w,%%a1\n"
		"	lea		0x4ba.w,%%a0\n"
		"	moveq	#2,%%d2\n"
		"	moveq	#50,%%d1\n"
		"	add.l	(%%a0),%%d2\n"
		"	add.l	%%d2,%%d1\n"
		"tstart:\n"
		"	stop    #0x2300\n"
		"	cmp.l	(%%a0),%%d2\n"	/* time to start ? */
		"	bcc.s	tstart\n"
		"	move.l	(%%a0),%%d2\n"
		"	move.b	#1,(%%a1)\n"	/* SB_PLA_ENA; start replay */
		"	nop\n"
		"tloop:\n"
		"	stop    #0x2300\n"
		"	tst.b	(%%a1)\n"		/* end of buffer ? */
		"	beq.s	tstop\n"
		"	cmp.l	(%%a0),%%d1\n"	/* time limit reached ? */
		"	bcc.s	tloop\n"
		"	clr.b	(%%a1)\n"		/* turn off replay */
		"tstop:\n"
		"	move.l	(%%a0),%%d0\n"	/* stop time */
		"	sub.l	%%d2,%%d0\n"	/* timelength */

		: "=r"(ret)	/* outputs */
		: /* inputs */
		: __CLOBBER_RETURN("d0") "d1", "d2", "a0", "a1", "cc" AND_MEMORY
	);

	return ret;
}

static int clock_type(long ticks)
{
	if (ticks <= 35)	/* [1-35] U [42-50] 49 kHz (type 0), 179 ms = 35 ticks */
		return 0;
	if (ticks >= 42)
		return 0;
	if (ticks <= 38)	/* [36-38] 48 kHz (type 2), 183 ms = 36 ticks */
		return 2;
	return 1;			/* [39-41] 44.1kHz (type 1), 200 ms = 40 ticks */
}


static long read_gpio(void)
{
	return *((unsigned char *)0xffff8943);
}


static void Mint_CheckExternalClock(_THIS)
{
#define SIZE_BUF_CLOCK_MEASURE (8820)

	char *buffer;
	int i;
	int is_aranym = 0;

	buffer = Atari_SysMalloc(SIZE_BUF_CLOCK_MEASURE, MX_STRAM);
	if (buffer == NULL)
	{
		DEBUG_PRINT((DEBUG_NAME "Not enough memory for the measure\n"));
		return;
	}
	memset(buffer, 0, SIZE_BUF_CLOCK_MEASURE);

	/*
	 * FIXME: workaround for ARAnyM,
	 * the measurement is not reliable
	 */
	Gpio(GPIO_WRITE, 0x08);
	/* ARAnyM masks the value, but HW does not */
	if (Supexec(read_gpio) == 0)
		is_aranym = 1;

	Sndstatus(SND_RESET);
	Buffoper(0);
	Settracks(0, 0);
	Setmontracks(0);
	Devconnect(DMAPLAY, DAC, CLKEXT, CLK50K, NO_SHAKE);	/* Matrix and clock source */
	Setmode(MODE_MONO);
	Jdisint(MFP_DMASOUND);
	Soundcmd(ADDERIN, MATIN);
	Setbuffer(SR_PLAY, buffer, buffer + SIZE_BUF_CLOCK_MEASURE);	/* Set buffer */

	for (i = 0; i < 2; i++)
	{
		int type;
		int gpio_bits = 2 + i;

		if (is_aranym)
		{
			type = i == 0 ? 1 : 2;
		} else
		{
			Gpio(GPIO_SET, 7);				/* DSP port gpio outputs */
			Gpio(GPIO_WRITE, gpio_bits);	/* 22.5792/24.576 MHz for 44.1/48KHz */
			if ((Gpio(GPIO_READ, SND_INQUIRE) & 3) == gpio_bits)
			{
				unsigned long ticks = Supexec(externalClockTest);
				type = clock_type(ticks);
				DEBUG_PRINT((DEBUG_NAME "measure %d: ticks=%lu type=%d\n", i + 1, ticks, type));
			} else
			{
				type = 0;
			}
		}
		switch (type)
		{
		case 1:
			/* CD */
			SDL_MintAudio_AddFrequency(this, 44100, CLKEXT, CLK50K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 29400, CLKEXT, CLK33K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 22050, CLKEXT, CLK25K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 17640, CLKEXT, CLK20K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 14700, CLKEXT, CLK16K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 11025, CLKEXT, CLK12K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this,  8820, CLKEXT, CLK10K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this,  7350, CLKEXT, CLK8K,  -1, gpio_bits);
			break;
		case 2:
			/* DAT */
			SDL_MintAudio_AddFrequency(this, 48000, CLKEXT, CLK50K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 32000, CLKEXT, CLK33K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 24000, CLKEXT, CLK25K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 19200, CLKEXT, CLK20K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 16000, CLKEXT, CLK16K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this, 12000, CLKEXT, CLK12K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this,  9600, CLKEXT, CLK10K, -1, gpio_bits);
			SDL_MintAudio_AddFrequency(this,  8000, CLKEXT, CLK8K,  -1, gpio_bits);
			break;
		}
	}

	Mfree(buffer);
}


static long Mint_StopReplay(void)
{
	/* Stop replay */
	DMAAUDIO_IO.control = 0;
	return 0;
}

static long Mint_StartReplay(void)
{
	/* Start replay */
	DMAAUDIO_IO.control = SB_PLA_ENA | SB_PLA_RPT;
	return 0;
}

static void stop_replay(void)
{
	if ((short)Buffoper(0) == 0x88) /* XBIOS function not available */
		Supexec(Mint_StopReplay);
}

static void start_replay(void)
{
	if ((short)Buffoper(SB_PLA_ENA | SB_PLA_RPT) == 0x88) /* XBIOS function not available */
		Supexec(Mint_StartReplay);
}

static void Mint_LockAudio(_THIS)
{
	/* Stop replay */
	stop_replay();
}

static void Mint_UnlockAudio(_THIS)
{
	/* Restart replay */
	start_replay();
}

static void Mint_CloseAudio(_THIS)
{
	/* Stop replay */
	stop_replay();

	if (oldGpio >= 0)
		Gpio(GPIO_WRITE, oldGpio);
	Soundcmd(ADDERIN, oldAdderIn);
	Soundcmd(ADCINPUT, oldAdcInput);
	Soundcmd(SETPRESCALE, oldPrescale);
 
	/* Uninstall interrupt */
	Jdisint(MFP_DMASOUND);

	SDL_MintAudio_FreeBuffers();

	/* Unlock sound system */
	Unlocksnd();
}

static void Mint_SwapBuffers(Uint8 *nextbuf, int nextsize)
{
	if ((short)Setbuffer(SR_PLAY, nextbuf, nextbuf + nextsize) == 0x83)
	{
		void *old_stack;
		unsigned long buffer;

		old_stack = (void *) Super(0);

		buffer = (unsigned long) nextbuf;
		DMAAUDIO_IO.start_high = (buffer >> 16) & 255;
		DMAAUDIO_IO.start_mid = (buffer >> 8) & 255;
		DMAAUDIO_IO.start_low = buffer & 255;

		buffer += nextsize;
		DMAAUDIO_IO.end_high = (buffer >> 16) & 255;
		DMAAUDIO_IO.end_mid = (buffer >> 8) & 255;
		DMAAUDIO_IO.end_low = buffer & 255;

		SuperToUser(old_stack);
	}
}

static void Mint_InitAudio(_THIS, SDL_AudioSpec *spec)
{
	int channels_mode;
	short ret;

	/* Stop currently playing sound */
	stop_replay();

	/* Set replay tracks */
	Settracks(0, 0);
	Setmontracks(0);

	/* Select replay format */
	switch (spec->format & 0xff)
	{
	case 8:
		if (spec->channels == 2)
		{
			channels_mode = MODE_STEREO8;
		} else
		{
			channels_mode = MODE_MONO8;
		}
		break;
	case 16:
		if (spec->channels == 2)
		{
			channels_mode = MODE_STEREO16;
		} else
		{
			channels_mode = MODE_MONO16;
		}
		break;
	default:
		return;
	}
	ret = Setmode(channels_mode);
	if (ret == 0x8c) /* XBIOS function not available */
	{
		void *oldstack = (void *) Super(0);
		unsigned char modectrl;

		modectrl = DMAAUDIO_IO.sound_ctrl & 0x3f;
		switch (channels_mode)
		{
		case MODE_MONO8:
			modectrl |= 0x80;
			break;
		case MODE_MONO16:
			modectrl |= 0x40 | 0x80;
			break;
		case MODE_STEREO8:
			modectrl |= 0;
			break;
		case MODE_STEREO16:
			modectrl |= 0x40;
			break;
		}
		DMAAUDIO_IO.sound_ctrl = modectrl;
		SuperToUser(oldstack);
	} else if (ret < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "Setmode() failed\n"));
	}

	if (hasFreeFrequency)
	{
		Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
		Soundcmd(SETSMPFREQ, spec->freq);
	} else if (this->hidden->frequencies[this->hidden->numfreq].gpio_bits >= 0)
	{
		Gpio(GPIO_SET, 7);				/* DSP port gpio outputs */
		Gpio(GPIO_WRITE, this->hidden->frequencies[this->hidden->numfreq].gpio_bits);
		Devconnect(DMAPLAY, DAC | EXTOUT, CLKEXT, this->hidden->frequencies[this->hidden->numfreq].predivisor, NO_SHAKE);
	} else if (this->hidden->frequencies[this->hidden->numfreq].predivisor == CLKOLD)
	{
		Devconnect(DMAPLAY, DAC, CLKOLD, this->hidden->frequencies[this->hidden->numfreq].predivisor_old, NO_SHAKE);
	} else
	{
		Devconnect(DMAPLAY, DAC, this->hidden->frequencies[this->hidden->numfreq].clocksrc, this->hidden->frequencies[this->hidden->numfreq].predivisor, NO_SHAKE);
	}

	Soundcmd(ADDERIN, MATIN);			/* set matrix to the adder */

	/* Set buffer */
	Mint_SwapBuffers(this->hidden->audiobuf[0], this->hidden->audiosize);

	/* Install interrupt */
	Jdisint(MFP_DMASOUND);
	Xbtimer(XB_TIMERA, 8, 1, SDL_MintAudio_XbiosInterrupt);
	Jenabint(MFP_DMASOUND);

	if (Setinterrupt(SI_TIMERA, SI_PLAY) < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "Setinterrupt() failed\n"));
	}

	/* Go */
	start_replay();
	DEBUG_PRINT((DEBUG_NAME "hardware initialized\n"));
}

static int Mint_CheckAudio(_THIS, SDL_AudioSpec *spec)
{
	int has16bitMono = 0;

	DEBUG_PRINT((DEBUG_NAME "asked: %d bits, ", spec->format & 0x00ff));
	DEBUG_PRINT(("signed=%d, ", ((spec->format & 0x8000) != 0)));
	DEBUG_PRINT(("big endian=%d, ", ((spec->format & 0x1000) != 0)));
	DEBUG_PRINT(("channels=%d, ", spec->channels));
	DEBUG_PRINT(("freq=%d\n", spec->freq));

	if (spec->channels > 2)
	{
		spec->channels = 2;				/* no more than stereo! */
	}

	if ((spec->format & 0x00ff) > 16)
		spec->format = (spec->format & ~0xff) | 16; /* no more than 16 bits */

	/* Check replay format */
	if (cookie_snd & SND_EXT)
	{
		has16bitMono = 1;
	}
	switch (spec->format & 0xff)
	{
	case 8:
		if (spec->channels == 2 && !has8bitStereo)
			spec->channels = 1;
		break;
	case 16:
		spec->format |= 0x1000;			/* Audio is always big endian */
		if (spec->channels == 1 && !has16bitMono)
			spec->channels = 2;
		break;
	default:
		return -1;
	}

	spec->format |= 0x8000;				/* Audio is always signed */

	oldAdderIn = Soundcmd(ADDERIN, SND_INQUIRE);
	if (oldAdderIn == 0x82) /* XBIOS function not available */
		oldAdderIn = 0;
	oldAdcInput = Soundcmd(ADCINPUT, SND_INQUIRE);
	if (oldAdcInput == 0x82) /* XBIOS function not available */
		oldAdcInput = 0;
	oldPrescale = Soundcmd(SETPRESCALE, SND_INQUIRE);
	if (oldPrescale == 0x82) /* XBIOS function not available */
		oldPrescale = 0;
	/* DSP present with its GPIO port ? */
	oldGpio = Gpio(GPIO_READ, SND_INQUIRE);	/* 'data' is ignored */
	if (oldGpio < 0 || oldGpio == 0x8a) /* XBIOS function not available */
		oldGpio = -1;

	this->hidden->freq_count = 0;

	if (oldGpio >= 0 && !hasFreeFrequency)
	{
		/* Add external clocks if present */
		Mint_CheckExternalClock(this);
	}

	/* reset connection matrix (and other settings) */
	Sndstatus(SND_RESET);

	if (hasFreeFrequency)
	{
		/* only need to add a single entry */
		SDL_MintAudio_AddFrequency(this, 50066, CLK25M, CLKOLD, -1, -1);
		spec->freq = Soundcmd(SETSMPFREQ, spec->freq);
	} else
	{
		if (Getcookie(C_McSn, NULL) == C_FOUND && !(cookie_snd & SND_16BIT))
		{
			/*
			 * hack for X-SOUND which doesn't understand SETPRESCALE
			 * and yet happily pretends that Falcon frequencies are
			 * STE/TT ones
			 */
			SDL_MintAudio_AddFrequency(this, 50066, CLK25M, CLK50K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 25033, CLK25M, CLK25K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 12517, CLK25M, CLK12K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 6258, CLK25M, 15, -1, -1);
		} else
		{
			/* STE/TT clocks */
			SDL_MintAudio_AddFrequency(this, 50066, CLK25M, CLKOLD, PRE160, -1);
			SDL_MintAudio_AddFrequency(this, 25033, CLK25M, CLKOLD, PRE320, -1);
			SDL_MintAudio_AddFrequency(this, 12517, CLK25M, CLKOLD, PRE640, -1);
			/* 6258 is muted on falcon */
			if (!(cookie_snd & SND_16BIT))
				SDL_MintAudio_AddFrequency(this, 6258, CLK25M, CLKOLD, PRE1280, -1);
		}

		if (cookie_snd & SND_16BIT)
		{
			/* Standard clocks */
			SDL_MintAudio_AddFrequency(this, 49170, CLK25M, CLK50K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 32780, CLK25M, CLK33K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 24585, CLK25M, CLK25K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 19668, CLK25M, CLK20K, -1, -1);
			SDL_MintAudio_AddFrequency(this, 16390, CLK25M, CLK16K, -1, -1);
			/* 14049 hz invalid for CODEC */
			SDL_MintAudio_AddFrequency(this, 12292, CLK25M, CLK12K, -1, -1);
			/* 10927 hz invalid for CODEC */
			SDL_MintAudio_AddFrequency(this, 9834, CLK25M, CLK10K, -1, -1);
			/* 8940 hz invalid for CODEC */
			SDL_MintAudio_AddFrequency(this, 8195, CLK25M, CLK8K, -1, -1);
			/* 7565 hz invalid */
			/* 7024 hz invalid */
			/* 6556 hz invalid */
			/* 6146 hz invalid */
		}
	}

	SDL_MintAudio_SetFrequency(this, spec);

	return 0;
}

static int Mint_OpenAudio(_THIS, SDL_AudioSpec *spec)
{
	short locked;

	/* Lock sound system */
	locked = (short)Locksnd();
	if (locked != 1 && locked != 0x80) /* XBIOS function not available */
	{
		SDL_SetError("Mint_OpenAudio: Audio system already in use");
		return -1;
	}

	SDL_MintAudio_device = this;

	/* Check audio capabilities */
	if (Mint_CheckAudio(this, spec) < 0)
	{
		Unlocksnd();
		return -1;
	}

	if (!SDL_MintAudio_InitBuffers(spec))
	{
		Unlocksnd();
		return -1;
	}

	/* Setup audio hardware */
	this->hidden->swapbuf = Mint_SwapBuffers;
	Mint_InitAudio(this, spec);

	return 1;							/* We don't use SDL threaded audio */
}

static SDL_AudioDevice *Audio_CreateDevice(int devindex)
{
	SDL_AudioDevice *this;

	(void)devindex;
	/* Initialize all variables that we clean on shutdown */
	this = (SDL_AudioDevice *) SDL_calloc(1, sizeof(*this));
	if (this)
	{
		this->hidden = (struct SDL_PrivateAudioData *) SDL_calloc(1, sizeof(*this->hidden));
	}
	if (this == NULL || this->hidden == NULL)
	{
		SDL_OutOfMemory();
		SDL_free(this);
		return 0;
	}

	/* Set the function pointers */
	this->OpenAudio = Mint_OpenAudio;
	this->CloseAudio = Mint_CloseAudio;
	this->LockAudio = Mint_LockAudio;
	this->UnlockAudio = Mint_UnlockAudio;
	this->free = Audio_DeleteDevice;

	return this;
}

AudioBootStrap MINTAUDIO_XBIOS_bootstrap = {
	MINT_AUDIO_DRIVER_NAME, "MiNT XBIOS audio driver", Audio_Available, Audio_CreateDevice
};
