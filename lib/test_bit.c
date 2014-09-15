/*
** Copyright (C) 2014 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
**
** This file is part of msprime.
**
** msprime is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** msprime is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with msprime.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Binary index tree (also known as a Fenwick tree) implementation.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "util.h"
#include "bit.h"

#include <stdio.h>
#include <assert.h>

static void
test_simple_cases(void)
{
    bit_t t;
    unsigned int n, j;
    long s;
    for (n = 1; n < 100; n++) {
        s = 0;
        t.max_index = n;
        bit_alloc(&t);
        for (j = 1; j <= n; j++) {
            bit_increment(&t, j, j);
            s += j;
            assert(bit_get_value(&t, j) == j);
            assert(bit_get_cumulative_sum(&t, j) == s);
            assert(bit_get_total(&t) == s);
            assert(bit_find(&t, s) == j);
            bit_set_value(&t, j, 0);
            assert(bit_get_value(&t, j) == 0);
            assert(bit_get_cumulative_sum(&t, j) == s - j);
            bit_set_value(&t, j, j);
            assert(bit_get_value(&t, j) == j);

        }
        bit_free(&t);
    }
}


int
main(int argc, char** argv)
{
    test_simple_cases();
    /* TODO add some proper test cases */
    return EXIT_SUCCESS;

}
