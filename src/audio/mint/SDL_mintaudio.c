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
	Audio interrupt variables and callback function

	Patrice Mandin
*/

#include <unistd.h>

#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/mintbind.h>
#include <mint/cookie.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include "../../video/ataricommon/SDL_atarimxalloc_c.h"

#include "SDL_mintaudio.h"

/* The audio device */

#define MAX_DMA_BUF	8

SDL_AudioDevice *SDL_MintAudio_device;

static int SDL_MintAudio_num_upd;		/* Number of calls to update function */
static int SDL_MintAudio_max_buf;		/* Number of buffers to use */
static int SDL_MintAudio_numbuf;		/* Buffer to play */

static void SDL_MintAudio_Callback(void);

/* Debug print info */
#define DEBUG_NAME "audio:mint: "
#if 0
#define DEBUG_PRINT(what) \
	{ \
		printf what; \
	}
#else
#define DEBUG_PRINT(what)
#endif

/* Initialize DMA buffers */

int SDL_MintAudio_InitBuffers(SDL_AudioSpec *spec)
{
	int dmabuflen;
	SDL_AudioDevice *this = SDL_MintAudio_device;

	if (!this)
		return 0;

	SDL_CalculateAudioSpec(spec);
	this->hidden->audiosize = spec->size * MAX_DMA_BUF;

	/* Allocate audio buffer memory for application in FastRAM */
	this->hidden->fastrambuf = Atari_SysMalloc(this->hidden->audiosize, MX_TTRAM);
	if (this->hidden->fastrambuf)
	{
		SDL_memset(this->hidden->fastrambuf, spec->silence, this->hidden->audiosize);
	}

	/* Allocate audio buffers memory for hardware in DMA-able RAM */
	dmabuflen = ((2 * this->hidden->audiosize) | 3) + 1;
	this->hidden->audiobuf[0] = Atari_SysMalloc(dmabuflen, MX_STRAM);
	if (this->hidden->audiobuf[0] == NULL)
	{
		SDL_SetError("SDL_MintAudio_InitBuffers: Not enough memory for audio buffer");
		return 0;
	}
	this->hidden->audiobuf[1] = this->hidden->audiobuf[0] + this->hidden->audiosize;
	SDL_memset(this->hidden->audiobuf[0], spec->silence, dmabuflen);

	DEBUG_PRINT((DEBUG_NAME "buffer 0 at 0x%p\n", this->hidden->audiobuf[0]));
	DEBUG_PRINT((DEBUG_NAME "buffer 1 at 0x%p\n", this->hidden->audiobuf[1]));

	SDL_MintAudio_numbuf = SDL_MintAudio_num_its = SDL_MintAudio_num_upd = 0;
	SDL_MintAudio_max_buf = MAX_DMA_BUF;

	/* For filling silence when too many interrupts per update */
	SDL_MintAudio_itbuffer = this->hidden->audiobuf[0];
	SDL_MintAudio_itbuflen = (dmabuflen >> 2) - 1;
	SDL_MintAudio_itsilence = (spec->silence << 24) | (spec->silence << 16) | (spec->silence << 8) | spec->silence;

	return 1;
}

/* Destroy DMA buffers */

void SDL_MintAudio_FreeBuffers(void)
{
	SDL_AudioDevice *this = SDL_MintAudio_device;

	if (!this)
		return;

	if (this->hidden->fastrambuf)
	{
		Mfree(this->hidden->fastrambuf);
		this->hidden->fastrambuf = NULL;
	}
	if (this->hidden->audiobuf[0])
	{
		Mfree(this->hidden->audiobuf[0]);
		SDL_MintAudio_itbuffer = this->hidden->audiobuf[0] = this->hidden->audiobuf[1] = NULL;
	}
}

/* Update buffers */

void SDL_AtariMint_UpdateAudio(void)
{
	SDL_AudioDevice *this = SDL_MintAudio_device;

	if (!this)
		return;

	++SDL_MintAudio_num_upd;

	/* No interrupt triggered? still playing current buffer */
	if (SDL_MintAudio_num_its == 0)
	{
		return;
	}

	if (SDL_MintAudio_num_upd < (SDL_MintAudio_num_its << 2))
	{
		/* Too many interrupts per update, increase latency */
		if (SDL_MintAudio_max_buf < MAX_DMA_BUF)
		{
			SDL_MintAudio_max_buf <<= 1;
		}
	} else if (SDL_MintAudio_num_its < (SDL_MintAudio_num_upd << 2))
	{
		/* Too many updates per interrupt, decrease latency */
		if (SDL_MintAudio_max_buf > 1)
		{
			SDL_MintAudio_max_buf >>= 1;
		}
	}
	this->hidden->audiosize = this->spec.size * SDL_MintAudio_max_buf;

	SDL_MintAudio_num_its = 0;
	SDL_MintAudio_num_upd = 0;

	SDL_MintAudio_numbuf ^= 1;

	/* Fill new buffer */
	SDL_MintAudio_Callback();

	/* And swap to it */
	(*this->hidden->swapbuf) (this->hidden->audiobuf[SDL_MintAudio_numbuf], this->hidden->audiosize);
}

/* The callback function, called by each driver whenever needed */

static void SDL_MintAudio_Callback(void)
{
	SDL_AudioDevice *this = SDL_MintAudio_device;
	Uint8 *buffer;
	int i;

	buffer = (this->hidden->fastrambuf ? this->hidden->fastrambuf : this->hidden->audiobuf[SDL_MintAudio_numbuf]);
	SDL_memset(buffer, this->spec.silence, this->spec.size * SDL_MintAudio_max_buf);

	if (!this->paused)
	{
		for (i = 0; i < SDL_MintAudio_max_buf; i++)
		{
			if (this->convert.needed)
			{
				int silence;

				if (this->convert.src_format == AUDIO_U8)
				{
					silence = 0x80;
				} else
				{
					silence = 0;
				}
				SDL_memset(this->convert.buf, silence, this->convert.len);
				this->spec.callback(this->spec.userdata, (Uint8 *) this->convert.buf, this->convert.len);
				SDL_ConvertAudio(&this->convert);
				SDL_memcpy(buffer, this->convert.buf, this->convert.len_cvt);

				buffer += this->convert.len_cvt;
			} else
			{
				this->spec.callback(this->spec.userdata, buffer, this->spec.size);

				buffer += this->spec.size;
			}
		}
	}

	if (this->hidden->fastrambuf)
	{
		SDL_memcpy(this->hidden->audiobuf[SDL_MintAudio_numbuf], this->hidden->fastrambuf, this->spec.size * SDL_MintAudio_max_buf);
	}
}

/* Add a new frequency/clock/predivisor to the current list */
void SDL_MintAudio_AddFrequency(_THIS, Uint32 frequency, Uint16 clock, Uint16 prediv, Sint16 prediv_old, int gpio_bits)
{
	int i;
	int p;

	if (this->hidden->freq_count == MINTAUDIO_maxfreqs)
	{
		return;
	}

	/* Search where to insert the frequency (highest first) */
	for (p = 0; p < this->hidden->freq_count; p++)
	{
		if (frequency > this->hidden->frequencies[p].frequency)
		{
			break;
		}
	}

	/* Put all following ones farer */
	if (this->hidden->freq_count > 0)
	{
		for (i = this->hidden->freq_count; i > p; i--)
		{
			this->hidden->frequencies[i] = this->hidden->frequencies[i - 1];
		}
	}

	/* And insert new one */
	this->hidden->frequencies[p].frequency = frequency;
	this->hidden->frequencies[p].clocksrc = clock;
	this->hidden->frequencies[p].predivisor = prediv;
	this->hidden->frequencies[p].predivisor_old = prediv_old;
	this->hidden->frequencies[p].gpio_bits = gpio_bits;

	this->hidden->freq_count++;
}

/* Search for the nearest frequency */
static int SDL_MintAudio_SearchFrequency(_THIS, int desired_freq)
{
	int i;

	/* Only 1 freq ? */
	if (this->hidden->freq_count == 1)
	{
		return 0;
	}

	/* Check the array */
	for (i = 0; i < this->hidden->freq_count; i++)
	{
		if (desired_freq >= ((this->hidden->frequencies[i].frequency + this->hidden->frequencies[i + 1].frequency) >> 1))
		{
			return i;
		}
	}

	/* Not in the array, give the latest */
	return this->hidden->freq_count - 1;
}

void SDL_MintAudio_SetFrequency(_THIS, SDL_AudioSpec *spec)
{
	int i;

	for (i = 0; i < this->hidden->freq_count; i++)
	{
		DEBUG_PRINT((DEBUG_NAME "freq %d: %u Hz, clock %u, prediv %d, gpio %d\n",
			i, this->hidden->frequencies[i].frequency, this->hidden->frequencies[i].clocksrc,
			this->hidden->frequencies[i].predivisor == CLKOLD ? this->hidden->frequencies[i].predivisor_old : this->hidden->frequencies[i].predivisor,
			this->hidden->frequencies[i].gpio_bits));
	}

	this->hidden->numfreq = SDL_MintAudio_SearchFrequency(this, spec->freq);
	spec->freq = this->hidden->frequencies[this->hidden->numfreq].frequency;

	DEBUG_PRINT((DEBUG_NAME "obtained: %d bits, ", spec->format & 0x00ff));
	DEBUG_PRINT(("signed=%d, ", ((spec->format & 0x8000) != 0)));
	DEBUG_PRINT(("big endian=%d, ", ((spec->format & 0x1000) != 0)));
	DEBUG_PRINT(("channels=%d, ", spec->channels));
	DEBUG_PRINT(("freq=%d\n", spec->freq));
}
