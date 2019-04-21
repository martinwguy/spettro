/*
** Copyright (C) 2007-2015 Erik de Castro Lopo <erikd@mega-nerd.com>
** Modified (M) 2018 by Martin Guy <martinwguy@gmail.com>
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
#include "paint.h"
#include "ui.h"


#define ARRAY_LEN(x)		((int) (sizeof(x) / sizeof(x[0])))

static void kaiser(double *data, int datalen);
static void nuttall(double *data, int datalen);
static void hann(double *data, int datalen);
static void hamming(double *data, int datalen);
static void bartlett(double *data, int datalen);
static void blackman(double *data, int datalen);
static void dolph(double *data, int datalen);

static double besseli0(double x);

typedef struct stored_window {
    window_function_t wfunc;
    int datalen;
    double *window;
    struct stored_window *next;
} stored_window_t;

stored_window_t *stored_windows = NULL;

const char *
window_name(window_function_t w)
{
    switch (w) {
    case ANY:		return "any";
    case RECTANGULAR:	return "rectangular";
    case KAISER:	return "Kaiser";
    case NUTTALL:	return "Nuttall";
    case HANN:	 	return "Hann";
    case HAMMING:	return "Hamming";
    case BARTLETT:	return "Bartlett";
    case BLACKMAN:	return "Blackman";
    case DOLPH:		return "Dolph";
    default:	 	return "invalid";
    }
}

const char
window_key(window_function_t w)
{
    return "RKNHMBLD?"[w];
}

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
    new_window = Malloc(datalen * sizeof(*new_window));

    switch (wfunc) {
    case KAISER:	kaiser(new_window, datalen);	break;
    case NUTTALL:	nuttall(new_window, datalen);	break;
    case HANN:		hann(new_window, datalen);	break;
    case HAMMING:	hamming(new_window, datalen);	break;
    case BARTLETT:	bartlett(new_window, datalen);	break;
    case BLACKMAN:	blackman(new_window, datalen);	break;
    case DOLPH:		dolph(new_window, datalen);	break;
    default:
	fprintf(stderr, "Internal error: Unknown window_function.\n");
	exit(1);
    };

    if (new_window == NULL) {
	fprintf(stderr, "Window creation failed.\n");
	abort();
    }

    /* Remember this window for future use */
    {
	stored_window_t *new = Malloc(sizeof(*new));
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

void
free_windows()
{
    stored_window_t *w, *next_w;

    for (w = stored_windows; w != NULL; w=next_w) {
	next_w = w->next;
	free(w->window);
    }
    stored_windows = NULL;
}

void
next_window_function(void)
{
    window_function = (window_function + 1) % NUMBER_OF_WINDOW_FUNCTIONS;
    repaint_display(TRUE);
}

void
prev_window_function(void)
{
    window_function = (window_function + NUMBER_OF_WINDOW_FUNCTIONS - 1)
    		      % NUMBER_OF_WINDOW_FUNCTIONS;
    repaint_display(TRUE);
}

static void
kaiser(double *data, int datalen)
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
nuttall(double *data, int datalen)
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
hann(double *data, int datalen)
{
    int k;

    /*
     *	Hann window function from :
     *
     *	http://en.wikipedia.org/wiki/Window_function
     */

    for (k = 0; k < datalen ; k++)
	data[k] = 0.5 * (1.0 - cos(2.0 * M_PI * k / (datalen - 1)));
}

static void
hamming(double *data, int datalen)
{
    int k;
    double m = datalen - 1;

    /* From sox spectrogram */
    for (k = 0; k < datalen ; k++)
	data[k] = .53836 - .46164 * cos(2 * M_PI * (double)k / m);
}

static void
bartlett(double *data, int datalen)
{
    int k;
    double m = datalen - 1;

    /* From sox spectrogram */
    for (k = 0; k < datalen ; k++)
	data[k] = 2.0 / m * (m/2 - fabs(k - m/2));
}

static void
blackman(double *data, int datalen)
{
    int k;
    double m = datalen - 1;
    double alpha = .16;

    /* From sox spectrogram */
    for (k = 0; k < datalen ; k++) {
	double x = 2 * M_PI * k / m;
	data[k] = 0.5 * ((1 - alpha) - cos(x) + alpha * cos(2 * x));
    }
}

static void
dolph(double *data, int N)
{
    double att = 126.6;	/* empirically */
    double b = cosh(acosh(pow(10., att/20)) / (N-1)), sum, t, c, norm = 0;
    int i, j;
    for (c = 1 - 1 / (b*b), i = (N-1) / 2; i >= 0; --i) {
      for (sum = !i, b = t = j = 1; j <= i && sum != t; b *= (i-j) * (1./j), ++j)
	t = sum, sum += (b *= c * (N - i - j) * (1./j));
      sum /= (N - 1 - i), sum /= (norm = norm ? norm : sum);
      data[i] = sum, data[N - 1 - i] = sum;
    }
}

static double
besseli0(double x)
{
    int k = 1;
    double half_x = 0.5 * x;
    double pow_half_x_k = half_x;	/* Always == pow(0.5*x, k) */
    double factorial_k = 1.0;
    double result = 0.0;

    while (k < 25) {
	double temp;

	temp = pow_half_x_k / factorial_k;
	result += temp * temp;

	k++; pow_half_x_k *= half_x; factorial_k *= k;
    }

    return 1.0 + result;
}
