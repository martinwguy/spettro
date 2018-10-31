/*
** Copyright (C) 2007-2015 Erik de Castro Lopo <erikd@mega-nerd.com>
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 or version 3 of the
** License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef WINDOW_H

typedef enum {
    ANY = -1,
    RECTANGULAR,
    KAISER,
    NUTTALL,
    HANN,
} window_function_t;

extern double *get_window(window_function_t wfunc, int datalen);
extern const char *window_name(window_function_t wfunc);

#define WINDOW_H
#endif
