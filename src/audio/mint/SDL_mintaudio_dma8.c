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
	using DMA 8bits (hardware access)

	Patrice Mandin
*/

/* Mint includes */
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include "../../video/ataricommon/SDL_atarisuper.h"

#include "SDL_mintaudio.h"
#include "SDL_mintaudio_dma8.h"

/*--- Defines ---*/

#define MINT_AUDIO_DRIVER_NAME "mint_dma8"

/* Debug print info */
#define DEBUG_NAME "audio:dma8: "
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
static long cookie_mch;

/*--- Audio driver functions ---*/

static int Audio_Available(void)
{
	const char *envr = getenv("SDL_AUDIODRIVER");

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

	/* Check if we have 8 bits audio */
	if ((cookie_snd & SND_8BIT) == 0)
	{
		DEBUG_PRINT((DEBUG_NAME "no 8 bits sound\n"));
		return 0;
	}

	/* Check if audio is lockable */
	if (cookie_snd & SND_16BIT)
	{
		short locked = (short)Locksnd();
		if (Locksnd() != 1 && locked != 84)
		{
			DEBUG_PRINT((DEBUG_NAME "audio locked by other application\n"));
			return 0;
		}

		Unlocksnd();
	}

	DEBUG_PRINT((DEBUG_NAME "8 bits audio available!\n"));
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
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

static void start_replay(void)
{
	Supexec(Mint_StartReplay);
}

static void stop_replay(void)
{
	Supexec(Mint_StopReplay);
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
	Supexec(Mint_StopReplay);

	DEBUG_PRINT((DEBUG_NAME "closeaudio: replay stopped\n"));

	/* Disable interrupt */
	Jdisint(MFP_DMASOUND);

	DEBUG_PRINT((DEBUG_NAME "closeaudio: interrupt disabled\n"));

	SDL_MintAudio_FreeBuffers();
}

static void Mint_SwapBuffersSuper(Uint8 *nextbuf, int nextsize)
{
	unsigned long buffer;

	buffer = (unsigned long) nextbuf;
	DMAAUDIO_IO.start_high = (buffer >> 16) & 255;
	DMAAUDIO_IO.start_mid = (buffer >> 8) & 255;
	DMAAUDIO_IO.start_low = buffer & 255;

	buffer += nextsize;
	DMAAUDIO_IO.end_high = (buffer >> 16) & 255;
	DMAAUDIO_IO.end_mid = (buffer >> 8) & 255;
	DMAAUDIO_IO.end_low = buffer & 255;
}

static void Mint_SwapBuffers(Uint8 *nextbuf, int nextsize)
{
	void *old_stack;

	/* Set first ticks value */
	old_stack = (void *) Super(0);

	Mint_SwapBuffersSuper(nextbuf, nextsize);

	SuperToUser(old_stack);
}

static long Mint_InitDma(void)
{
	SDL_AudioDevice *this = SDL_MintAudio_device;
	unsigned char mode;

	Mint_StopReplay();

	/* Set buffer */
	Mint_SwapBuffersSuper(this->hidden->audiobuf[0], this->hidden->audiosize);

	if (this->hidden->frequencies[this->hidden->numfreq].predivisor == CLKOLD)
		mode = this->hidden->frequencies[this->hidden->numfreq].predivisor_old;
	else
		mode = this->hidden->frequencies[this->hidden->numfreq].predivisor;
	if (this->spec.channels == 1)
	{
		mode |= 1 << 7;
	}
	if (this->hidden->frequencies[this->hidden->numfreq].gpio_bits >= 0)
	{
		DMAAUDIO_IO.data_direction = 7;
		DMAAUDIO_IO.dev_data = this->hidden->frequencies[this->hidden->numfreq].gpio_bits;
	}
	DMAAUDIO_IO.sound_ctrl = mode;
	return 0;
}

static int Mint_CheckAudio(_THIS, SDL_AudioSpec *spec)
{
	DEBUG_PRINT((DEBUG_NAME "asked: %d bits, ", spec->format & 0x00ff));
	DEBUG_PRINT(("signed=%d, ", ((spec->format & 0x8000) != 0)));
	DEBUG_PRINT(("big endian=%d, ", ((spec->format & 0x1000) != 0)));
	DEBUG_PRINT(("channels=%d, ", spec->channels));
	DEBUG_PRINT(("freq=%d\n", spec->freq));

	if (spec->channels > 2)
		spec->channels = 2;

	/* Check formats available */
	spec->format = AUDIO_S8;

	/* Calculate and select the closest frequency */
	this->hidden->freq_count = 0;
	switch (cookie_mch >> 16)
	{
	case MCH_STE:
	case MCH_TT:
	default:
		SDL_MintAudio_AddFrequency(this, 50066, CLK25M, CLKOLD, PRE160, -1);
		SDL_MintAudio_AddFrequency(this, 25033, CLK25M, CLKOLD, PRE320, -1);
		SDL_MintAudio_AddFrequency(this, 12517, CLK25M, CLKOLD, PRE640, -1);
		SDL_MintAudio_AddFrequency(this, 6258, CLK25M, CLKOLD, PRE1280, -1);
		break;
	case MCH_ARANYM:
		SDL_MintAudio_AddFrequency(this, 44100, CLKEXT, CLK50K, -1, 0x02);
		SDL_MintAudio_AddFrequency(this, 22050, CLKEXT, CLK25K, -1, 0x02);
		SDL_MintAudio_AddFrequency(this, 11025, CLKEXT, CLK12K, -1, 0x02);
		SDL_MintAudio_AddFrequency(this, 48000, CLKEXT, CLK50K, -1, 0x03);
		SDL_MintAudio_AddFrequency(this, 24000, CLKEXT, CLK25K, -1, 0x03);
		SDL_MintAudio_AddFrequency(this, 12000, CLKEXT, CLK12K, -1, 0x03);
		/* fall through */
	case MCH_F30:
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
		break;
	}

	SDL_MintAudio_SetFrequency(this, spec);

	return 0;
}

static int Mint_OpenAudio(_THIS, SDL_AudioSpec *spec)
{
	SDL_MintAudio_device = this;

	/* Check audio capabilities */
	if (Mint_CheckAudio(this, spec) < 0)
	{
		return -1;
	}

	if (!SDL_MintAudio_InitBuffers(spec))
	{
		return -1;
	}

	this->hidden->swapbuf = Mint_SwapBuffers;

	/* Set replay tracks */
	if (cookie_snd & SND_16BIT)
	{
		Settracks(0, 0);
		Setmontracks(0);
	}

	Supexec(Mint_InitDma);

	/* Set interrupt */
	Jdisint(MFP_DMASOUND);
	Xbtimer(XB_TIMERA, 8, 1, SDL_MintAudio_XbiosInterrupt);
	Jenabint(MFP_DMASOUND);

	if (cookie_snd & SND_16BIT)
	{
		if (Setinterrupt(SI_TIMERA, SI_PLAY) < 0)
		{
			DEBUG_PRINT((DEBUG_NAME "Setinterrupt() failed\n"));
		}
	}

	start_replay();

	return 1;							/* We don't use threaded audio */
}

static SDL_AudioDevice *Audio_CreateDevice(int devindex)
{
	SDL_AudioDevice *this;

	/* Initialize all variables that we clean on shutdown */
	this = (SDL_AudioDevice *) SDL_calloc(1, sizeof(SDL_AudioDevice));
	if (this)
	{
		this->hidden = (struct SDL_PrivateAudioData *) SDL_calloc(1, sizeof(*this->hidden));
	}
	if (this == NULL || this->hidden == NULL)
	{
		SDL_OutOfMemory();
		if (this)
		{
			SDL_free(this);
		}
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

AudioBootStrap MINTAUDIO_DMA8_bootstrap = {
	MINT_AUDIO_DRIVER_NAME, "MiNT DMA 8 bits audio driver", Audio_Available, Audio_CreateDevice
};
