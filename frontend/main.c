/*
 *      Command line frontend program
 *
 *      Copyright (c) 1999 Mark Taylor
 *                    2000 Takehiro TOMINAGA
 *                    2010-2012 Robert Hegemann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 main.c is example code for how to use libmp3lame.a.  To use this library,
 you only need the library and lame.h.  All other .h files are private
 to the library.
*/
#include "lame.h"

#include "console.h"
#include "main.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static int c_main(int argc, char *argv[]);

FILE*
lame_fopen(char const* file, char const* mode)
{
    return fopen(file, mode);
}

char*
lame_getenv(char const* var)
{
    char* str = getenv(var);
    if (str != NULL) {
        return strdup(str);
    }
    return NULL;
}

int
main(int argc, char *argv[])
{
    return c_main(argc, argv);
}

static int
c_main(int argc, char *argv[])
{
    lame_t  gf;
    int     ret;

    frontend_open_console();
    gf = lame_init(); /* initialize libmp3lame */
    if (NULL == gf) {
        error_printf("fatal error during initialization\n");
        ret = 1;
    }
    else {
        ret = lame_main(gf, argc, argv);
        lame_close(gf);
    }
    frontend_close_console();
    return ret;
}
