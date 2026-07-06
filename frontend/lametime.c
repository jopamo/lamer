/*
 *	Lame time routines source file
 *
 *	Copyright (c) 2000 Mark Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

/*
 * name:        GetCPUTime ( void )
 *
 * description: returns CPU time used by the process
 * input:       none
 * output:      time in seconds
 * known bugs:  may not work in SMP and RPC
 * conforming:  ANSI C
 *
 * There is some old difficult to read code at the end of this file.
 * Can someone integrate this into this function (if useful)?
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "lametime.h"

#if !defined(CLOCKS_PER_SEC)
# warning Your system does not define CLOCKS_PER_SEC, guessing one...
# define CLOCKS_PER_SEC 1000000
#endif


double
GetCPUTime(void)
{
    return clock() / (double) CLOCKS_PER_SEC;
}


/*
 * name:        GetRealTime ( void )
 *
 * description: returns real (human) time elapsed relative to a fixed time (mostly 1970-01-01 00:00:00)
 * input:       none
 * output:      time in seconds
 * known bugs:  bad precision with time()
 */

double
GetRealTime(void)
{
    struct timeval t;

    if (0 != gettimeofday(&t, NULL))
        assert(0);
    return t.tv_sec + 1.e-6 * t.tv_usec;
}

int
lame_set_stream_binary_mode(FILE * const fp)
{
    (void) fp;          /* doing nothing here, silencing the compiler only. */
    return 0;
}


/* End of lametime.c */
