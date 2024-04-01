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
	using XBIOS functions (STFA driver)

	Patrice Mandin
*/

/* Mint includes */
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include "SDL_mintaudio.h"
#include "SDL_mintaudio_stfa.h"

/*--- Defines ---*/

#define MINT_AUDIO_DRIVER_NAME "mint_stfa"

/* Debug print info */
#define DEBUG_NAME "audio:stfa: "
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
static cookie_stfa_t *cookie_stfa;

static const int freqs[16] = {
	4995, 6269, 7493, 8192,
	9830, 10971, 12538, 14985,
	16384, 19819, 21943, 24576,
	30720, 32336, 43885, 49152
};

/*--- interrupt function ---*/
/*
 * same as SDL_MintAudio_XbiosInterrupt(),
 * but do not touch the MFP
 */
__attribute__((interrupt))
static void SDL_MintAudio_StfaInterrupt(void)
{
	unsigned long num_its;
	unsigned long *buffer;
	long buflen;
	
	num_its = SDL_MintAudio_num_its;
	++num_its;
	SDL_MintAudio_num_its = num_its;
	if (num_its >= 5)
	{
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

static int Audio_Available(void)
{
	long dummy;
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

	/* Cookie STFA present ? */
	if (Getcookie(C_STFA, &dummy) != C_FOUND)
	{
		DEBUG_PRINT((DEBUG_NAME "no STFA audio\n"));
		return 0;
	}
	cookie_stfa = (cookie_stfa_t *) dummy;

	DEBUG_PRINT((DEBUG_NAME "STFA audio available!\n"));
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static void stop_replay(void)
{
	cookie_stfa->sound_enable = STFA_PLAY_DISABLE;
}

static void start_replay(void)
{
	cookie_stfa->sound_enable = STFA_PLAY_ENABLE | STFA_PLAY_REPEAT;
}

static void Mint_LockAudio(_THIS)
{
	stop_replay();
}

static void Mint_UnlockAudio(_THIS)
{
	start_replay();
}

static void Mint_CloseAudio(_THIS)
{
	stop_replay();

	SDL_MintAudio_FreeBuffers();
}

static void Mint_SwapBuffers(Uint8 *nextbuf, int nextsize)
{
	cookie_stfa->sound_start = (unsigned long) nextbuf;
	cookie_stfa->sound_end = cookie_stfa->sound_start + nextsize;
}

static void Mint_InitAudio(_THIS, SDL_AudioSpec *spec)
{
	/* Stop replay */
	stop_replay();

	/* Select replay format */
	cookie_stfa->sound_control = this->hidden->frequencies[this->hidden->numfreq].predivisor_old;
	if ((spec->format & 0xff) == 8)
	{
		cookie_stfa->sound_control |= STFA_FORMAT_8BIT;
	} else
	{
		cookie_stfa->sound_control |= STFA_FORMAT_16BIT;
	}
	if (spec->channels == 2)
	{
		cookie_stfa->sound_control |= STFA_FORMAT_STEREO;
	} else
	{
		cookie_stfa->sound_control |= STFA_FORMAT_MONO;
	}
	if ((spec->format & 0x8000) != 0)
	{
		cookie_stfa->sound_control |= STFA_FORMAT_SIGNED;
	} else
	{
		cookie_stfa->sound_control |= STFA_FORMAT_UNSIGNED;
	}
	if ((spec->format & 0x1000) != 0)
	{
		cookie_stfa->sound_control |= STFA_FORMAT_BIGENDIAN;
	} else
	{
		cookie_stfa->sound_control |= STFA_FORMAT_LITENDIAN;
	}

	/* Set buffer */
	Mint_SwapBuffers(this->hidden->audiobuf[0], this->hidden->audiosize);

	/* Set interrupt */
	cookie_stfa->stfa_it = SDL_MintAudio_StfaInterrupt;

	/* Restart replay */
	start_replay();

	DEBUG_PRINT((DEBUG_NAME "hardware initialized\n"));
}

static int Mint_CheckAudio(_THIS, SDL_AudioSpec *spec)
{
	int i;

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
	for (i = 0; i < 16; i++)
	{
		SDL_MintAudio_AddFrequency(this, freqs[i], CLK25M, CLKOLD, i, -1);
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

	/* Setup audio hardware */
	this->hidden->swapbuf = Mint_SwapBuffers;
	Mint_InitAudio(this, spec);

	return 1;							/* We don't use threaded audio */
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

AudioBootStrap MINTAUDIO_STFA_bootstrap = {
	MINT_AUDIO_DRIVER_NAME, "MiNT STFA audio driver", Audio_Available, Audio_CreateDevice
};
