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
	using XBIOS functions (MacSound compatible driver)

	Patrice Mandin
*/

#include <support.h>

/* Mint includes */
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include "SDL_mintaudio.h"
#include "SDL_mintaudio_mcsn.h"

/*--- Defines ---*/

#define MINT_AUDIO_DRIVER_NAME "mint_mcsn"

/* Debug print info */
#define DEBUG_NAME "audio:mcsn: "
#if 0
#define DEBUG_PRINT(what) \
	{ \
		printf what; \
	}
#else
#define DEBUG_PRINT(what)
#endif

/*--- Static variables ---*/

static long cookie_mch;
static cookie_mcsn_t *cookie_mcsn;

/*--- Audio driver functions ---*/

static int Audio_Available(void)
{
	long dummy;
	const char *envr = getenv("SDL_AUDIODRIVER");
	long cookie_snd;

	/* We can't use XBIOS in interrupt with Magic, don't know about thread */
	if (Getcookie(C_MagX, &dummy) == C_FOUND)
	{
		return 0;
	}

	/* Check if user asked a different audio driver */
	if (envr && strcmp(envr, MINT_AUDIO_DRIVER_NAME) != 0)
	{
		DEBUG_PRINT((DEBUG_NAME "user asked a different audio driver\n"));
		return 0;
	}

	/* Cookie _MCH present ? if not, assume ST machine */
	if (Getcookie(C__MCH, &cookie_mch) == C_NOTFOUND)
	{
		cookie_mch = MCH_ST;
	}

	/* Cookie _SND present ? if not, assume ST machine */
	if (Getcookie(C__SND, &cookie_snd) == C_NOTFOUND)
	{
		cookie_snd = SND_PSG;
	}

	/* Check if we have 16 bits audio */
	if ((cookie_snd & SND_16BIT) == 0)
	{
		DEBUG_PRINT((DEBUG_NAME "no 16 bits sound\n"));
		return 0;
	}

	/* Cookie MCSN present ? */
	if (Getcookie(C_McSn, &dummy) != C_FOUND)
	{
		DEBUG_PRINT((DEBUG_NAME "no MCSN audio\n"));
		return 0;
	}
	cookie_mcsn = (cookie_mcsn_t *) dummy;

	/* Check if interrupt at end of replay */
	if (cookie_mcsn->pint == 0)
	{
		DEBUG_PRINT((DEBUG_NAME "no interrupt at end of replay\n"));
		return 0;
	}

	/* Check if audio is lockable */
	if (Locksnd() != 1)
	{
		DEBUG_PRINT((DEBUG_NAME "audio locked by other application\n"));
		return 0;
	}

	Unlocksnd();

	DEBUG_PRINT((DEBUG_NAME "MCSN audio available!\n"));
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static void stop_replay(void)
{
	Buffoper(0);
}

static void start_replay(void)
{
	Buffoper(SB_PLA_ENA | SB_PLA_RPT);
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

	/* Uninstall interrupt */
	Jdisint(MFP_DMASOUND);

	SDL_MintAudio_FreeBuffers();

	/* Unlock sound system */
	Unlocksnd();
}

static void Mint_SwapBuffers(Uint8 *nextbuf, int nextsize)
{
	Setbuffer(SR_PLAY, nextbuf, nextbuf + nextsize);
}

static void Mint_InitAudio(_THIS, SDL_AudioSpec *spec)
{
	int channels_mode;
	int prediv;

	/* Stop currently playing sound */
	stop_replay();

	/* Set replay tracks */
	Settracks(0, 0);
	Setmontracks(0);

	/* Select replay format */
	channels_mode = STEREO16;
	switch (spec->format & 0xff)
	{
	case 8:
		if (spec->channels == 2)
		{
			channels_mode = STEREO8;
		} else
		{
			channels_mode = MONO8;
		}
		break;
	}
	if (Setmode(channels_mode) < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "Setmode() failed\n"));
	}

	prediv = this->hidden->frequencies[this->hidden->numfreq].predivisor;
	switch (cookie_mcsn->play)
	{
	case MCSN_TT:
		Devconnect(DMAPLAY, DAC, CLK25M, CLKOLD, NO_SHAKE);
		if (prediv == CLKOLD && this->hidden->frequencies[this->hidden->numfreq].predivisor_old >= 0)
			Soundcmd(SETPRESCALE, this->hidden->frequencies[this->hidden->numfreq].predivisor_old);
		DEBUG_PRINT((DEBUG_NAME "STE/TT prescaler selected\n"));
		break;
	case MCSN_FALCON:
		Devconnect(DMAPLAY, DAC, this->hidden->frequencies[this->hidden->numfreq].clocksrc, prediv, NO_SHAKE);
		if (prediv == CLKOLD && this->hidden->frequencies[this->hidden->numfreq].predivisor_old >= 0)
			Soundcmd(SETPRESCALE, this->hidden->frequencies[this->hidden->numfreq].predivisor_old);
		DEBUG_PRINT((DEBUG_NAME "Falcon prescaler selected\n"));
		break;
	}

	/* Set buffer */
	Mint_SwapBuffers(this->hidden->audiobuf[0], this->hidden->audiosize);

	DEBUG_PRINT((DEBUG_NAME "Using Timer-A\n"));
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
	DEBUG_PRINT((DEBUG_NAME "asked: %d bits, ", spec->format & 0x00ff));
	DEBUG_PRINT(("signed=%d, ", ((spec->format & 0x8000) != 0)));
	DEBUG_PRINT(("big endian=%d, ", ((spec->format & 0x1000) != 0)));
	DEBUG_PRINT(("channels=%d, ", spec->channels));
	DEBUG_PRINT(("freq=%d\n", spec->freq));

	if (spec->channels > 2)
	{
		spec->channels = 2;				/* no more than stereo! */
	}

	/* Check formats available */
	this->hidden->freq_count = 0;
	switch (cookie_mcsn->play)
	{
	case MCSN_ST:
		/* ST does not have DMA audio, but that should be checked in the _SND cookie */
		spec->channels = 1;
		spec->format = AUDIO_U8;				/* FIXME: is it signed or unsigned ? */
		SDL_MintAudio_AddFrequency(this, 12500, CLK25M, CLKOLD, -1, -1);
		break;
	case MCSN_TT:						/* Also STE, Mega STE */
		spec->format = AUDIO_S8;
		SDL_MintAudio_AddFrequency(this, 50066, CLK25M, CLKOLD, PRE160, -1);
		SDL_MintAudio_AddFrequency(this, 25033, CLK25M, CLKOLD, PRE320, -1);
		SDL_MintAudio_AddFrequency(this, 12517, CLK25M, CLKOLD, PRE640, -1);
		SDL_MintAudio_AddFrequency(this, 6258, CLK25M, CLKOLD, PRE1280, -1);
		break;
	case MCSN_FALCON:					/* Also Mac */
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
		/* 6146 hz not usable on falcon */
		if (cookie_mcsn->res1 != 0)
		{
			SDL_MintAudio_AddFrequency(this, cookie_mcsn->res1 / (MASTERPREDIV_FALCON * 2), CLKEXT, CLK50K, -1, -1);
			SDL_MintAudio_AddFrequency(this, cookie_mcsn->res1 / (MASTERPREDIV_FALCON * 4), CLKEXT, CLK25K, -1, -1);
			SDL_MintAudio_AddFrequency(this, cookie_mcsn->res1 / (MASTERPREDIV_FALCON * 8), CLKEXT, CLK12K, -1, -1);
		}
		spec->format |= 0x8000;			/* Audio is always signed */
		if ((spec->format & 0x00ff) == 16)
		{
			spec->format |= 0x1000;		/* Audio is always big endian */
			spec->channels = 2;			/* 16 bits always stereo */
		}
		break;
	default:
		return -1;
	}

	SDL_MintAudio_SetFrequency(this, spec);

	return 0;
}

static int Mint_OpenAudio(_THIS, SDL_AudioSpec *spec)
{
	/* Lock sound system */
	if (Locksnd() != 1)
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

AudioBootStrap MINTAUDIO_MCSN_bootstrap = {
	MINT_AUDIO_DRIVER_NAME, "MiNT MCSN audio driver", Audio_Available, Audio_CreateDevice
};
