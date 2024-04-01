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
	DMA 8bits and Falcon Codec audio definitions

	Patrice Mandin, Didier Méquignon
*/

#ifndef _SDL_mintaudio_dma8_h
#define _SDL_mintaudio_dma8_h

#define DMAAUDIO_IO_BASE (0xffff8900)
struct DMAAUDIO_IO_S {
	unsigned char int_ctrl;              /* 0xffff8900 */
	unsigned char control;               /* 0xffff8901 */

	unsigned char dummy1;
	unsigned char start_high;            /* 0xffff8903 */
	unsigned char dummy2;
	unsigned char start_mid;             /* 0xffff8905 */
	unsigned char dummy3;
	unsigned char start_low;             /* 0xffff8907 */

	unsigned char dummy4;
	unsigned char cur_high;              /* 0xffff8909 */
	unsigned char dummy5;
	unsigned char cur_mid;               /* 0xffff890b */
	unsigned char dummy6;
	unsigned char cur_low;               /* 0xffff890d */

	unsigned char dummy7;
	unsigned char end_high;              /* 0xffff890f */
	unsigned char dummy8;
	unsigned char end_mid;               /* 0xffff8911 */
	unsigned char dummy9;
	unsigned char end_low;               /* 0xffff8913 */

	unsigned char dummy10[12];

	unsigned char track_ctrl;            /* 0xffff8920 CODEC only */
	unsigned char sound_ctrl;            /* 0xffff8921 */
	unsigned short sound_data;           /* 0xffff8922 */
	unsigned short sound_mask;           /* 0xffff8924 */

	unsigned char dummy11[10];
	
	unsigned short dev_ctrl;             /* 0xffff8930 */
	unsigned short dest_ctrl;            /* 0xffff8932 */
	unsigned char freq_ext;              /* 0xffff8934 */
	unsigned char freq_int;              /* 0xffff8935 */
	unsigned char track_rec;             /* 0xffff8936 */
	unsigned char adderin_input;         /* 0xffff8937 */
	unsigned char channel_input;         /* 0xffff8938 */
	unsigned char channel_amplification; /* 0xffff8939 */
	unsigned char channel_reduction;     /* 0xffff893a */
	
	unsigned char dummy12[6];

	unsigned char data_direction;        /* 0xffff8941 */
	unsigned char dummy13;               /* 0xffff8941 */
	unsigned char dev_data;              /* 0xffff8943 */
};
#define DMAAUDIO_IO ((*(volatile struct DMAAUDIO_IO_S *)DMAAUDIO_IO_BASE))

#endif /* _SDL_mintaudio_dma8_h */
