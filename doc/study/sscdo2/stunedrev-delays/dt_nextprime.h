// Davide Tedesco's nextprime.h (SSCDO#2 repository, src/FAUST/targets/stunedrev),
// functions renamed dt_* so that it links beside SEAM's src/h/nextprime.h.
#include <stdio.h>
#include <math.h>

int dt_is_prime(int num);
int dt_next_pr(int num);

int dt_next_pr(int num){
    int c;
    if(num < 2)
        c = 2;
    else if (num == 2)
        c = 3;
    else if(num & 1){
        num += 2;
        c = dt_is_prime(num) ? num : dt_next_pr(num);
    } else
        c = dt_next_pr(num-1);

    return c;
}

int dt_is_prime(int num){
    if((num & 1)==0)
        return num == 2;
    else {
        int i, limit = sqrt(num);
        for (i = 3; i <= limit; i+=2){
            if (num % i == 0)
                return 0;
        }
    }
    return 1;
}
