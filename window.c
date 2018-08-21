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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "spettro.h"
#include "window.h"

#define ARRAY_LEN(x)		((int) (sizeof(x) / sizeof(x[0])))

static double besseli0(double x);
static double factorial(int k);

static enum WINDOW_FUNCTION current_window_function = RECTANGULAR;
static int current_datalen = 0;
static double *current_window = NULL;
static double current_beta = 0.0;

double *
kaiser_window(int datalen, double beta)
{
    /*
     *         besseli0(beta * sqrt(1 - (2*x/N).^2))
     * w(x) =  --------------------------------------,  -N/2 <= x <= N/2
     *                 besseli0(beta)
     */

    double two_n_on_N, denom;
    double *data;
    int k;

    if (current_window != NULL &&
	current_window_function == KAISER &&
	current_datalen == datalen &&
	current_beta == beta) return(current_window);

    /* Don't free old windows because a calc thread may still be using them */
    current_window = malloc(datalen * sizeof(double));
    if (current_window == NULL) {
	fputs("Out of memory.\n", stderr);
	exit(1);
    }
    data = current_window;
    current_window_function = KAISER;
    current_datalen = datalen;
    current_beta = beta;

    denom = besseli0(beta);

    if (!isfinite(denom)) {
	printf("besseli0(%f) : %f\nExiting\n", beta, denom);
	exit(1);
    }

    for (k = 0; k < datalen ; k++) {
	double n = k + 0.5 - 0.5 * datalen;
	two_n_on_N = (2.0 * n) / datalen;
	data[k] = besseli0(beta * sqrt(1.0 - two_n_on_N * two_n_on_N)) / denom;
    }

    return(data);
}

double *
nuttall_window(int datalen)
{
    const double a[4] = { 0.355768, 0.487396, 0.144232, 0.012604 };
    double *data;
    int k;

    if (current_window_function == NUTTALL && current_datalen == datalen)
	return(current_window);

    /* Don't free old windows because a calc thread may still be using them */
    current_window = malloc(datalen * sizeof(double));
    if (current_window == NULL) {
	fputs("Out of memory.\n", stderr);
	exit(1);
    }
    data = current_window;
    current_window_function = NUTTALL;
    current_datalen = datalen;

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

    return(data);
}

double *
hann_window(int datalen)
{
    double *data;
    int k;

    if (current_window_function == HANN && current_datalen == datalen)
	return(current_window);

    /* Don't free old windows because a calc thread may still be using them */
    current_window = malloc(datalen * sizeof(double));
    if (current_window == NULL) {
	fputs("Out of memory.\n", stderr);
	exit(1);
    }
    data = current_window;
    current_window_function = HANN;
    current_datalen = datalen;

    /*
     *	Hann window function from :
     *
     *	http://en.wikipedia.org/wiki/Window_function
     */

    for (k = 0; k < datalen ; k++) {
	data[k] = 0.5 * (1.0 - cos(2.0 * M_PI * k / (datalen - 1)));
    }

    return(data);
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
