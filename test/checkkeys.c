
/* Simple program:  Loop, watching keystrokes
   Note that you need to call SDL_PollEvent() or SDL_WaitEvent() to 
   pump the event loop and catch keystrokes.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

static void print_modifiers(void)
{
	int mod;
	printf(" modifiers:");
	mod = SDL_GetModState();
	if(!mod) {
		printf(" (none)");
		return;
	}
	if(mod & KMOD_LSHIFT)
		printf(" LSHIFT");
	if(mod & KMOD_RSHIFT)
		printf(" RSHIFT");
	if(mod & KMOD_LCTRL)
		printf(" LCTRL");
	if(mod & KMOD_RCTRL)
		printf(" RCTRL");
	if(mod & KMOD_LALT)
		printf(" LALT");
	if(mod & KMOD_RALT)
		printf(" RALT");
	if(mod & KMOD_LMETA)
		printf(" LMETA");
	if(mod & KMOD_RMETA)
		printf(" RMETA");
	if(mod & KMOD_NUM)
		printf(" NUM");
	if(mod & KMOD_CAPS)
		printf(" CAPS");
	if(mod & KMOD_MODE)
		printf(" MODE");
}

static void PrintKey(SDL_keysym *sym, int pressed)
{
	/* Print the keycode, name and state */
	if ( sym->sym ) {
		printf("Key %s:  %d-%s (scancode = %d [0x%x])", pressed ?  "pressed" : "released",
					sym->sym, SDL_GetKeyName(sym->sym), sym->scancode, sym->scancode);
	} else {
		printf("Unknown Key (scancode = %d [0x%x]) %s ", sym->scancode, sym->scancode,
					pressed ?  "pressed" : "released");
	}

	/* Print the translated character, if one exists */
	if ( sym->unicode ) {
		/* Is it a control-character? */
		if ( sym->unicode < ' ' ) {
			printf(" (^%c)", sym->unicode+'@');
		} else {
#ifdef UNICODE
			printf(" '%c' (0x%.4X)", sym->unicode, (int)sym->unicode);
#elif defined(_WIN32) || defined(__OS2__)
			/* This is a Latin-1 program, so only show 8-bits */
			if ( !(sym->unicode & 0xFF00) )
				printf(" '%c' (0x%.4X)", sym->unicode, (int)sym->unicode);
			else
				printf(" (0x%.4X)", (int)sym->unicode);
#else /* other platforms than Windows hopefully use UTF-8 for 8bit chars */
			const char* utf32type = (SDL_BYTEORDER == SDL_LIL_ENDIAN) ? "UTF-32LE" : "UTF-32BE";
			char* utf8str;
			Uint32 utf32str[2];
			utf32str[0] = sym->unicode;
			utf32str[1] = 0;
			utf8str = SDL_iconv_string("UTF-8", utf32type, (const char*)utf32str, 2*4);
			printf(" '%s' (0x%.4X)", utf8str, (int)sym->unicode);
			SDL_free(utf8str);
#endif
		}
	}
	print_modifiers();
	printf("\n");
}

int main(int argc, char *argv[])
{
	SDL_Event event;
	int done;
	Uint32 videoflags;

#if defined(SDL_SLB) && (defined(__atarist__) || defined(__TOS__))
	{
		long slbret;
		if ((slbret = slb_sdl_open(NULL)) < 0)
		{
			fprintf(stderr, "cannot load " SDL_SHAREDLIB_NAME ": %ld\n", slbret);
			return 1;
		}
	}
#endif

	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(1);
	}

	videoflags = SDL_SWSURFACE;
	while( argc > 1 ) {
		--argc;
		if ( argv[argc] && !strcmp(argv[argc], "-fullscreen") ) {
			videoflags |= SDL_FULLSCREEN;
		} else {
			fprintf(stderr, "Usage: %s [-fullscreen]\n", argv[0]);
			quit(1);
		}
	}

	/* Set 640x480 video mode */
	if ( SDL_SetVideoMode(640, 480, 0, videoflags) == NULL ) {
		fprintf(stderr, "Couldn't set 640x480 video mode: %s\n",
							SDL_GetError());
		quit(2);
	}

	/* Enable UNICODE translation for keyboard input */
	SDL_EnableUNICODE(1);

	/* Enable auto repeat for keyboard input */
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
	                    SDL_DEFAULT_REPEAT_INTERVAL);

	/* Watch keystrokes */
	done = 0;
	while ( !done ) {
		/* Check for events */
		SDL_WaitEvent(&event);
		switch (event.type) {
			case SDL_KEYDOWN:
				PrintKey(&event.key.keysym, 1);
				break;
			case SDL_KEYUP:
				PrintKey(&event.key.keysym, 0);
				break;
			case SDL_MOUSEBUTTONDOWN:
				printf("mouse button down %d\n", event.button.button);
				break;
			case SDL_MOUSEBUTTONUP:
				printf("mouse button up %d\n", event.button.button);
				break;
			case SDL_QUIT:
				done = 1;
				break;
			default:
				break;
		}
	}

	SDL_Quit();
	return(0);
}
