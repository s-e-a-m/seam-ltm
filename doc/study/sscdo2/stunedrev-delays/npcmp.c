/* npcmp.c -- Davide Tedesco's next_pr against SEAM's, every n in [-10, 3 000 000].
 * Built twice by run.sh: as is, and with -DMUTATE, which drops the strictness of
 * SEAM's next_pr (returns n itself when n is odd); the second run must fail. */
#include <stdio.h>
#include "dt_nextprime.h"
#ifdef MUTATE
#include <math.h>
static int is_prime(int num) {
    if (num < 2) return 0; if (num < 4) return 1; if ((num & 1) == 0) return 0;
    int limit = (int)sqrt((double)num);
    for (int i = 3; i <= limit; i += 2) if (num % i == 0) return 0;
    return 1;
}
static int next_pr(int num) {
    if (num < 2) return 2;
    int c = (num & 1) ? num : num + 1;   /* mutated: num + 2 in SEAM */
    while (!is_prime(c)) c += 2;
    return c;
}
#else
#include "nextprime.h"
#endif
int main(void) {
    long bad = 0; int first = 0;
    for (int n = -10; n <= 3000000; n++)
        if (dt_next_pr(n) != next_pr(n)) { if (!bad) first = n; bad++; }
    printf("n in [-10, 3000000]: %ld mismatches%s", bad, bad ? "" : "\n");
    if (bad) printf(", first at n = %d\n", first);
    return bad ? 1 : 0;
}
