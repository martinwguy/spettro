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

static void kaiser(float *data, int datalen);
static void dolph(float *data, int datalen);
static void nuttall(float *data, int datalen);
static void blackman(float *data, int datalen);
static void hann(float *data, int datalen);

static float besseli0(float x);

typedef struct stored_window {
    window_function_t wfunc;
    int datalen;
    float *window;
    struct stored_window *next;
} stored_window_t;

stored_window_t *stored_windows = NULL;

const char *
window_name(window_function_t w)
{
    switch (w) {
    case KAISER:	return "Kaiser";
    case DOLPH:		return "Dolph";
    case NUTTALL:	return "Nuttall";
    case BLACKMAN:	return "Blackman";
    case HANN:	 	return "Hann";
    default:	 	return "invalid";
    }
}

const char
window_key(window_function_t w)
{
    return "KNHBD?"[w];
}

float *
get_window(window_function_t wfunc, int datalen)
{
    float *new_window;	/* data to return */

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
    case DOLPH:		dolph(new_window, datalen);	break;
    case NUTTALL:	nuttall(new_window, datalen);	break;
    case BLACKMAN:	blackman(new_window, datalen);	break;
    case HANN:		hann(new_window, datalen);	break;
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

static void
kaiser(float *data, int datalen)
{
    float beta = 20.0;
    /* beta = pi * alpha in the literature, so alpha =~ 6.3662 */

    /*
     *         besseli0(beta * sqrt(1 - (2*x/N).^2))
     * w(x) =  --------------------------------------,  -N/2 <= x <= N/2
     *                 besseli0(beta)
     */

    float two_n_on_N, denom;
    int k;

    denom = besseli0(beta);

    if (!isfinite(denom) || isnan(denom)) {
	printf("besseli0(%f) : %f\nExiting\n", beta, denom);
	exit(1);
    }

    for (k = 0; k < datalen ; k++) {
	float n = k + 0.5 - 0.5 * datalen;
	two_n_on_N = (2.0 * n) / datalen;
	data[k] = besseli0(beta * sqrt(1.0 - two_n_on_N * two_n_on_N)) / denom;
    }

}

static void
nuttall(float *data, int datalen)
{
    const float a[4] = { 0.355768, 0.487396, 0.144232, 0.012604 };
    int k;

    /*
     *	Nuttall window function from :
     *
     *	http://en.wikipedia.org/wiki/Window_function
     */

    for (k = 0; k < datalen ; k++) {
	float scale;

	scale = M_PI * k / (datalen - 1);

	data[k] = a[0]
		- a[1] * cos(2.0 * scale)
		+ a[2] * cos(4.0 * scale)
		- a[3] * cos(6.0 * scale);
    }
}

static void
hann(float *data, int datalen)
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
blackman(float *data, int datalen)
{
    int k;
    float m = datalen - 1;
    float alpha = .16;

    /* From sox spectrogram */
    for (k = 0; k < datalen ; k++) {
	float x = 2 * M_PI * k / m;
	data[k] = 0.5 * ((1 - alpha) - cos(x) + alpha * cos(2 * x));
    }
}

static void
dolph(float *data, int N)
{
    float att = 126.6;	/* empirically */
    float b = cosh(acosh(pow(10., att/20)) / (N-1)), sum, t, c, norm = 0;
    int i, j;
    for (c = 1 - 1 / (b*b), i = (N-1) / 2; i >= 0; --i) {
      for (sum = !i, b = t = j = 1; j <= i && sum != t; b *= (i-j) * (1./j), ++j)
	t = sum, sum += (b *= c * (N - i - j) * (1./j));
      sum /= (N - 1 - i), sum /= (norm = norm ? norm : sum);
      data[i] = sum, data[N - 1 - i] = sum;
    }
}

static float
besseli0(float x)
{
    int k = 1;
    float half_x = 0.5 * x;
    float pow_half_x_k = half_x;	/* Always == pow(0.5*x, k) */
    float factorial_k = 1.0;
    float result = 0.0;

    while (k < 25) {
	float temp;

	temp = pow_half_x_k / factorial_k;
	result += temp * temp;

	k++; pow_half_x_k *= half_x; factorial_k *= k;
    }

    return 1.0 + result;
}
