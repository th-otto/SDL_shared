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

/*
 * GSXB audio definitions
 * 
 * Patrice Mandin
 */

#ifndef _SDL_mintaudio_gsxb_h
#define _SDL_mintaudio_gsxb_h

/* Bit 5 in cookie _SND */

#ifndef SND_GSXB
#define SND_GSXB	(1<<5)
#endif

/* NSoundcmd modes */

#ifndef SETRATE
#define SETRATE			7	/* Set sample rate */
#define SET8BITFORMAT	8	/* 8 bits format */
#define SET16BITFORMAT	9	/* 16 bits format */
#define SET24BITFORMAT	10	/* 24 bits format */
#define SET32BITFORMAT	11	/* 32 bits format */
#define LTATTEN_MASTER	12	/* Attenuation */
#define RTATTEN_MASTER	13
#define LTATTEN_MICIN	14
#define RTATTEN_MICIN	15
#define LTATTEN_FMGEN	16
#define RTATTEN_FMGEN	17
#define LTATTEN_LINEIN	18
#define RTATTEN_LINEIN	19
#define LTATTEN_CDIN	20
#define RTATTEN_CDIN	21
#define LTATTEN_VIDIN	22
#define RTATTEN_VIDIN	23
#define LTATTEN_AUXIN	24
#define RTATTEN_AUXIN	25
#endif

/* Setmode modes */

#ifndef MODE_MONO8
#define MODE_MONO8 MODE_MONO
#endif
#ifndef MODE_MONO16
#define MODE_MONO16		3
#define MODE_STEREO24	4
#define MODE_STEREO32	5
#define MODE_MONO24		6
#define MODE_MONO32		7
#endif

/* Sndstatus modes */

#ifndef SND_QUERYFORMAT
#define SND_QUERYFORMATS	2
#define SND_QUERYMIXERS		3
#define SND_QUERYSOURCES	4
#define SND_QUERYDUPLEX		5
#define SND_QUERY8BIT		8
#define SND_QUERY16BIT		9
#define SND_QUERY24BIT		10
#define SND_QUERY32BIT		11

#define SND_FORMAT8		(1<<0)
#define SND_FORMAT16	(1<<1)
#define SND_FORMAT24	(1<<2)
#define SND_FORMAT32	(1<<3)

#define SND_FORMATSIGNED		(1<<0)
#define SND_FORMATUNSIGNED		(1<<1)
#define SND_FORMATBIGENDIAN		(1<<2)
#define SND_FORMATLITTLEENDIAN	(1<<3)
#endif

#endif /* _SDL_mintaudio_gsxb_h */
