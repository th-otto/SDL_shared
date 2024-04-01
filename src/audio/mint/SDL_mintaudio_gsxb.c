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
	using XBIOS functions (GSXB compatible driver)

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
#include "SDL_mintaudio_gsxb.h"

/*--- Defines ---*/

#define MINT_AUDIO_DRIVER_NAME "mint_gsxb"

/* Debug print info */
#define DEBUG_NAME "audio:gsxb: "
#if 0
#define DEBUG_PRINT(what) \
	{ \
		printf what; \
	}
#else
#define DEBUG_PRINT(what)
#endif

/*--- Static variables ---*/


/*--- Audio driver functions ---*/

static int Audio_Available(void)
{
	long cookie_snd;
	long cookie_gsxb;
	const char *envr = getenv("SDL_AUDIODRIVER");

	/* Check if user asked a different audio driver */
	if (envr && strcmp(envr, MINT_AUDIO_DRIVER_NAME) != 0)
	{
		DEBUG_PRINT((DEBUG_NAME "user asked a different audio driver\n"));
		return 0;
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

	/* Cookie GSXB present ? */
	/* Is it GSXB ? */
	if ((cookie_snd & SND_GSXB) == 0 || Getcookie(C_GSXB, &cookie_gsxb) != C_FOUND)
	{
		DEBUG_PRINT((DEBUG_NAME "no GSXB audio\n"));
		return 0;
	}

	/* Check if audio is lockable */
	if (Locksnd() != 1)
	{
		DEBUG_PRINT((DEBUG_NAME "audio locked by other application\n"));
		return 0;
	}

	Unlocksnd();

	DEBUG_PRINT((DEBUG_NAME "GSXB audio available!\n"));
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}


static void Mint_GsxbInterrupt(void)
{
	++SDL_MintAudio_num_its;
}

static void Mint_GsxbNullInterrupt(void)
{
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
	if (NSetinterrupt(2, SI_NONE, Mint_GsxbNullInterrupt) < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "NSetinterrupt() failed in close\n"));
	}

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
		channels_mode = STEREO16;
		break;
	}
	if (Setmode(channels_mode) < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "Setmode() failed\n"));
	}

	prediv = this->hidden->frequencies[this->hidden->numfreq].predivisor;
	Devconnect(DMAPLAY, DAC, CLKEXT, prediv, NO_SHAKE);

	/* Set buffer */
	Mint_SwapBuffers(this->hidden->audiobuf[0], this->hidden->audiosize);

	/* Install interrupt */
	if (NSetinterrupt(2, SI_PLAY, Mint_GsxbInterrupt) < 0)
	{
		DEBUG_PRINT((DEBUG_NAME "NSetinterrupt() failed\n"));
	}

	/* Go */
	start_replay();
	DEBUG_PRINT((DEBUG_NAME "hardware initialized\n"));
}

static int Mint_CheckAudio(_THIS, SDL_AudioSpec *spec)
{
	long snd_format = 0;
	int format_signed;
	int format_bigendian;
	Uint16 test_format = SDL_FirstAudioFormat(spec->format);
	int valid_datatype = 0;

	/*resolution = spec->format & 0x00ff; */
	format_signed = ((spec->format & 0x8000) != 0);
	format_bigendian = ((spec->format & 0x1000) != 0);

	DEBUG_PRINT((DEBUG_NAME "asked: %d bits, ", spec->format & 0x00ff));
	DEBUG_PRINT(("signed=%d, ", ((spec->format & 0x8000) != 0)));
	DEBUG_PRINT(("big endian=%d, ", ((spec->format & 0x1000) != 0)));
	DEBUG_PRINT(("channels=%d, ", spec->channels));
	DEBUG_PRINT(("freq=%d\n", spec->freq));

	if (spec->channels > 2)
	{
		spec->channels = 2;				/* no more than stereo! */
	}

	while (!valid_datatype && test_format != 0)
	{
		/* Check formats available */
		snd_format = Sndstatus(SND_QUERYFORMATS);
		spec->format = test_format;
		/*resolution = spec->format & 0xff; */
		format_signed = (spec->format & (1 << 15));
		format_bigendian = (spec->format & (1 << 12));
		switch (test_format)
		{
		case AUDIO_U8:
		case AUDIO_S8:
			if (snd_format & SND_FORMAT8)
			{
				valid_datatype = 1;
				snd_format = Sndstatus(SND_QUERY8BIT);
			}
			break;

		case AUDIO_U16LSB:
		case AUDIO_S16LSB:
		case AUDIO_U16MSB:
		case AUDIO_S16MSB:
			if (snd_format & SND_FORMAT16)
			{
				valid_datatype = 1;
				snd_format = Sndstatus(SND_QUERY16BIT);
			}
			break;

		default:
			test_format = SDL_NextAudioFormat();
			break;
		}
	}

	if (!valid_datatype)
	{
		SDL_SetError("Unsupported audio format");
		return -1;
	}

	/* Check signed/unsigned format */
	if (format_signed)
	{
		if (snd_format & SND_FORMATSIGNED)
		{
			/* Ok */
		} else if (snd_format & SND_FORMATUNSIGNED)
		{
			/* Give unsigned format */
			spec->format = spec->format & (~0x8000);
		}
	} else
	{
		if (snd_format & SND_FORMATUNSIGNED)
		{
			/* Ok */
		} else if (snd_format & SND_FORMATSIGNED)
		{
			/* Give signed format */
			spec->format |= 0x8000;
		}
	}

	if (format_bigendian)
	{
		if (snd_format & SND_FORMATBIGENDIAN)
		{
			/* Ok */
		} else if (snd_format & SND_FORMATLITTLEENDIAN)
		{
			/* Give little endian format */
			spec->format = spec->format & (~0x1000);
		}
	} else
	{
		if (snd_format & SND_FORMATLITTLEENDIAN)
		{
			/* Ok */
		} else if (snd_format & SND_FORMATBIGENDIAN)
		{
			/* Give big endian format */
			spec->format |= 0x1000;
		}
	}

	/* Calculate and select the closest frequency */
	this->hidden->freq_count = 0;
	SDL_MintAudio_AddFrequency(this, 44100, CLKEXT, CLK50K, -1, -1);
	SDL_MintAudio_AddFrequency(this, 22050, CLKEXT, CLK25K, -1, -1);
	SDL_MintAudio_AddFrequency(this, 11025, CLKEXT, CLK12K, -1, -1);

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

AudioBootStrap MINTAUDIO_GSXB_bootstrap = {
	MINT_AUDIO_DRIVER_NAME, "MiNT GSXB audio driver", Audio_Available, Audio_CreateDevice
};
