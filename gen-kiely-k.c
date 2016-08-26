#include <stdio.h>
#include <math.h>

/*
 * Tabulate k_min(dv) and k_max(dv).
 * See (2) in [Kiely].
 */

int main()
{
    if (1) {
	int prev_k1 = 0;
	printf("// k_min\n");
	for (unsigned dv = 3; dv < ~0U; dv++) {
	    int k1 = log2((dv+1.0)*2/3);
	    if (k1 < 6)
		continue;
	    if (k1 == prev_k1)
		continue;
	    printf("%2d %u\n", k1, dv);
	    if (k1 == 30)
		break;
	    prev_k1 = k1;
	}
    }
    if (1) {
	int prev_k2 = 0;
	printf("// k_max\n");
	for (unsigned dv = 3; dv < ~0U; dv++) {
	    int k2 = ceil(log2(dv));
	    if (k2 < 6)
		continue;
	    if (k2 == prev_k2)
		continue;
	    printf("%2d %u\n", k2, dv);
	    if (k2 == 30)
		break;
	    prev_k2 = k2;
	}
    }
    return 0;
}
