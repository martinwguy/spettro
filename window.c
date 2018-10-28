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

#include "spettro.h"
#include "window.h"

#include "lock.h"

#include <math.h>

#define ARRAY_LEN(x)		((int) (sizeof(x) / sizeof(x[0])))

static void kaiser_window(double *data, int datalen);
static void nuttall_window(double *data, int datalen);
static void hann_window(double *data, int datalen);

static double besseli0(double x);
static double factorial(int k);

typedef struct stored_window {
    window_function_t wfunc;
    int datalen;
    double *window;
    struct stored_window *next;
} stored_window_t;

stored_window_t *stored_windows = NULL;

double *
get_window(window_function_t wfunc, int datalen)
{
    double *new_window;	/* data to return */

    if (wfunc == RECTANGULAR) return NULL;

    lock_window();

    /* See if it's already in the cache */
    {	stored_window_t *w;
	for (w = stored_windows; w != NULL; w=w->next) {
	    if (w->wfunc == wfunc && w->datalen == datalen) {
		unlock_window();
		return(w->window);
	    }
	}
    }

    /* If not, make a new one and fill it */
    new_window = malloc(datalen * sizeof(double));
    if (new_window == NULL) {
	fprintf(stderr, "Out of memory in get_window()\n");
	exit(1);
    }

    switch (wfunc) {
    case KAISER:  kaiser_window(new_window, datalen);	break;
    case NUTTALL: nuttall_window(new_window, datalen);	break;
    case HANN:    hann_window(new_window, datalen);		break;
    default:      fprintf(stderr, "Internal error: Unknown window_function.\n");
		  exit(1);
    };

    if (new_window == NULL) {
	fprintf(stderr, "Window creation failed.\n");
	abort();
    }

    /* Remember this window for future use */
    {
	stored_window_t *new = malloc(sizeof(stored_window_t));
	if (new == NULL) {
	    fprintf(stderr, "Out of memory storing new window\n");
	    exit(1);
	}
	new->wfunc = wfunc;
	new->datalen = datalen;
	new->window = new_window;
	new->next = stored_windows;
	stored_windows = new;
    }

    unlock_window();
    return new_window;
}

static void
kaiser_window(double *data, int datalen)
{
    double beta = 20.0;

    /*
     *         besseli0(beta * sqrt(1 - (2*x/N).^2))
     * w(x) =  --------------------------------------,  -N/2 <= x <= N/2
     *                 besseli0(beta)
     */

    double two_n_on_N, denom;
    int k;

    denom = besseli0(beta);

    if (!isfinite(denom) || isnan(denom)) {
	printf("besseli0(%f) : %f\nExiting\n", beta, denom);
	exit(1);
    }

    for (k = 0; k < datalen ; k++) {
	double n = k + 0.5 - 0.5 * datalen;
	two_n_on_N = (2.0 * n) / datalen;
	data[k] = besseli0(beta * sqrt(1.0 - two_n_on_N * two_n_on_N)) / denom;
    }

}

static void
nuttall_window(double *data, int datalen)
{
    const double a[4] = { 0.355768, 0.487396, 0.144232, 0.012604 };
    int k;

    /*
     *	Nuttall window function from :
     *
     *	http://en.wikipedia.org/wiki/Window_function
     */

    for (k = 0; k < datalen ; k++) {
	double scale;

	scale = M_PI * k / (datalen - 1);

	data[k] = a[0]
		- a[1] * cos(2.0 * scale)
		+ a[2] * cos(4.0 * scale)
		- a[3] * cos(6.0 * scale);
    }
}

static void
hann_window(double *data, int datalen)
{
    int k;

    /*
     *	Hann window function from :
     *
     *	http://en.wikipedia.org/wiki/Window_function
     */

    for (k = 0; k < datalen ; k++) {
	data[k] = 0.5 * (1.0 - cos(2.0 * M_PI * k / (datalen - 1)));
    }
}

static double
besseli0(double x)
{
    int k;
    double result = 0.0;

    for (k = 1; k < 25; k++) {
	double temp;

	temp = pow(0.5 * x, k) / factorial(k);
	result += temp * temp;
    }

    return 1.0 + result;
}

static double
factorial(int val)
{
    static double memory[64] = { 1.0 };
    static int have_entry = 0;

    int k;

    if (val < 0) {
	printf("Oops : val < 0.\n");
	exit(1);
    }

    if (val > ARRAY_LEN(memory)) {
	printf("Oops : val > ARRAY_LEN(memory).\n");
	    exit(1);
    }

    if (val < have_entry)
	    return memory[val];

    for (k = have_entry + 1; k <= val ; k++)
	    memory[k] = k * memory[k - 1];

    have_entry = val;

    return memory[val];
}
