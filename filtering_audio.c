/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Main routine for the standalone test program filtering_audio program,
 * used for testing libav functionality.
 *
 * See filtering_audio() in libav.c
 */

#include "spettro.h"
#include "libav.h"

int
main(int argc, char **argv)
{
#if USE_LIBAV
    return filtering_main(argc, argv);
#else
    fprintf(stderr, "%s wasn't built with USE_LIBAV\n", argv[0]);
    return 1;
#endif
}
