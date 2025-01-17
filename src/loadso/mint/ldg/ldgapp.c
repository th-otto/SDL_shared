/*
 * LDG : Gem Dynamical Libraries
 * Copyright (c) 1997-2004 Olivier Landemarre, Dominique Bereziat & Arnaud Bercegeay
 *
 * Binding of functions provided by LDGM cookies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stddef.h>
#include <string.h>
#include "ldg.h"
#include "global.h"

/* Binding des fonctions du cookies */

LDG *ldg_open(const char *lib, short *gl)
{
	LDG_INFOS *cook;

	if (ldg_cookie(LDG_COOKIE, (long *) &cook) && cook->version >= 0x0200 && cook->ldg_open)
		return (*cook->ldg_open) (lib, gl);
	/* Si pas TSR, chargement direct */
	return ldg_load(gl[2], lib);
}


void *ldg_find(const char *name_fct, LDG *ldg)
{
	LDG_INFOS *cook;
	static int lastpos = 0;
	int i, j;

	if (ldg_cookie(LDG_COOKIE, (long *) &cook))
	{
		if (cook->version >= 0x200 && cook->ldg_find)
			return (*cook->ldg_find) (name_fct, ldg);
	}
	if (ldg == NULL)
	{
		ldg_set_error(LDG_NO_FUNC);
		return NULL;
	}
	for (i = 0; i < ldg->num; i++)
	{
		j = (i + lastpos) % ldg->num;
		if (!strcmp(ldg->list[j].name, name_fct))
		{
			lastpos = j + 1;
			return ((void *) ldg->list[j].func);
		}
	}
	return NULL;
}


short ldg_close(LDG *ldg, short *global)
{
	LDG_INFOS *cook;

	if (ldg_cookie(LDG_COOKIE, (long *) &cook) && cook->version >= 0x210 && cook->ldg_close)
		return (*cook->ldg_close) (ldg, global);
	/* Si pas de cookie, d�chargement direct */
	return ldg_unload(global[2], ldg);
}


/* 
 * Retourne l'erreur associ� � la fonction ldg_exec()
 *			 0 	pas d'erreur
 *			-1	plus de place pour d�clarer une lib	
 *			-2  plus de place pour d�clarer un client
 *			-3  lib non �xecutable (erreur Pexec)
 *			-4  mauvais format de lib
 *			-6  librairie v�rouill�e
 *			-8  lib non trouv�e
 *			-9  erreur m�moire
 *		    -11	pas de cookie
 *
 *	valeurs obsolettes avec la version 2.00 des LDG:
 *			-5  pas de r�ponse du manager
 *			-7  LDG manager non trouv�e 
 *			-10 Time Idle
 */

short ldg_error(void)
{
	LDG_INFOS *cook;

	if (ldg_cookie(LDG_COOKIE, (long *) &cook))
	{
		if (cook->version >= 0x210)
		{
			short err = cook->error;

			cook->error = 0;
			return err;
		} else
			return LDG_BAD_TSR;
	}
	return LDG_NO_TSR;
}

/*
 *	Cherche le chemin d'une librairie
 */

short ldg_libpath(const char *path, short *gl)
{
	LDG_INFOS *cook;

	if (ldg_cookie(LDG_COOKIE, (long *) &cook))
	{
		if (cook->version >= 0x0301 && cook->ldg_libpath)
			return (*cook->ldg_libpath) (path, gl);
		else
			return LDG_BAD_TSR;
	}
	return LDG_NO_TSR;
}
