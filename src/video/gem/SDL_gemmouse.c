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
 *	GEM Mouse manager
 *
 *	Patrice Mandin
 */

#include <gem.h>

#include "SDL_mouse.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_cursor_c.h"
#include "SDL_gemmouse_c.h"
#include "SDL_gemvideo.h"
#include "../ataricommon/SDL_xbiosevents_c.h"

/* Defines */

/*#define DEBUG_VIDEO_GEM 1*/

#ifdef DEBUG_VIDEO_GEM
#define DEBUG_VIDEO(x, ...) printf("sdl:video:gem: " x, ## __VA_ARGS__)
#else
#define DEBUG_VIDEO(x, ...)
#endif

#define MAXCURWIDTH 16
#define MAXCURHEIGHT 16

void GEM_FreeWMCursor(_THIS, WMcursor *cursor)
{
	DEBUG_VIDEO("free cursor\n");

	if (cursor == NULL)
		return;

	graf_mouse(ARROW, NULL);
	GEM_prev_cursor = NULL;

	if (cursor->mform_p != NULL)
		SDL_free(cursor->mform_p);

	SDL_free(cursor);
}

WMcursor *GEM_CreateWMCursor(_THIS,
		Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y)
{
	WMcursor *cursor;
	MFORM *new_mform;
	int i;

	DEBUG_VIDEO("create cursor\n");

	/* Check the size */
	if ( (w > MAXCURWIDTH) || (h > MAXCURHEIGHT) ) {
		SDL_SetError("Only cursors of dimension (%dx%d) are allowed",
							MAXCURWIDTH, MAXCURHEIGHT);
		return(NULL);
	}

	/* Allocate the cursor memory */
	cursor = (WMcursor *)SDL_malloc(sizeof(WMcursor));
	if ( cursor == NULL ) {
		SDL_OutOfMemory();
		return(NULL);
	}

	/* Allocate mform */
	new_mform = (MFORM *)SDL_malloc(sizeof(MFORM));
	if (new_mform == NULL) {
		SDL_free(cursor);
		SDL_OutOfMemory();
		return(NULL);
	}

	cursor->mform_p = new_mform;

	new_mform->mf_xhot = hot_x;
	new_mform->mf_yhot = hot_y;
	new_mform->mf_nplanes = 1;
	new_mform->mf_fg = 0;
	new_mform->mf_bg = 1;

	for (i=0;i<MAXCURHEIGHT;i++) {
		new_mform->mf_mask[i]=0;
		new_mform->mf_data[i]=0;
		DEBUG_VIDEO("source: line %d: data=0x%04x, mask=0x%04x\n",
			i, ((Uint16 *) &data[i<<1])[i], ((Uint16 *) &mask[i<<1])[i]);
	}

	if (w<=8) {
		for (i=0;i<h;i++) {
			new_mform->mf_mask[i]= mask[i]<<8;
			new_mform->mf_data[i]= data[i]<<8;
		}
	} else {
		for (i=0;i<h;i++) {
			new_mform->mf_mask[i]= (mask[i<<1]<<8) | mask[(i<<1)+1];
			new_mform->mf_data[i]= (data[i<<1]<<8) | data[(i<<1)+1];
		}
	}

	for (i=0; i<h ;i++) {
		DEBUG_VIDEO("cursor: line %d: data=0x%04x, mask=0x%04x\n",
			i, new_mform->mf_data[i], new_mform->mf_mask[i]);
	}

	DEBUG_VIDEO("CreateWMCursor(): done\n");

	return cursor;
}

int GEM_ShowWMCursor(_THIS, WMcursor *cursor)
{
	GEM_cursor = cursor;

	GEM_CheckMouseMode(this);

	DEBUG_VIDEO("ShowWMCursor(0x%08lx)\n", (long) cursor);

	return 1;
}

#if 0
void GEM_WarpWMCursor(_THIS, Uint16 x, Uint16 y)
{
	/* This seems to work only on AES 3.4 (Falcon) */

	EVNTREC	warpevent;

	warpevent.ap_event = APPEVNT_MOUSE;
	warpevent.ap_value = (x << 16) | y;

	appl_tplay(&warpevent, 1, 1000);
}
#endif

void GEM_CheckMouseMode(_THIS)
{
	const Uint8 full_focus = (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS);
	int set_system_cursor = 1;
	SDL_bool hide_system_cursor = SDL_FALSE;

	DEBUG_VIDEO("check mouse mode\n");

	/* If the mouse is hidden and input is grabbed, we use relative mode */
	GEM_mouse_relative = (!(SDL_cursorstate & CURSOR_VISIBLE))
		&& (this->input_grab != SDL_GRAB_OFF)
		&& (SDL_GetAppState() & SDL_APPACTIVE);
	SDL_AtariXbios_LockMousePosition(GEM_mouse_relative);

	if (SDL_cursorstate & CURSOR_VISIBLE) {
		/* Application defined cursor only over the application window */
		if ((SDL_GetAppState() & full_focus) == full_focus) {
			if (GEM_cursor) {
				if (GEM_cursor != GEM_prev_cursor) {
					graf_mouse(USER_DEF, GEM_cursor->mform_p);
					GEM_prev_cursor = GEM_cursor;
				}
				set_system_cursor = 0;
			} else {
				hide_system_cursor = SDL_TRUE;
			}
		}
	} else {
		/* Mouse cursor hidden only over the application window */
		if ((SDL_GetAppState() & full_focus) == full_focus) {
			set_system_cursor = 0;
			hide_system_cursor = SDL_TRUE;
		}
	}

	if (hide_system_cursor != GEM_cursor_hidden) {
		graf_mouse(hide_system_cursor ? M_OFF : M_ON, NULL);
		GEM_cursor_hidden = hide_system_cursor;
	}

	if (set_system_cursor && GEM_prev_cursor) {
		graf_mouse(ARROW, NULL);
		GEM_prev_cursor = NULL;
	}
}
