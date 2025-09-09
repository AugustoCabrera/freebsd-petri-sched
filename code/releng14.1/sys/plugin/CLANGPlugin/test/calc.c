#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <sys/metadata_payloads.h>

__PAYLOAD__;

int main() {

    clock_t start = clock();
        

    int result = 0;
    for (long i = 1; i <= 10000000000; ++i) {
        result += i * i;  // Example calculation
    }

    clock_t end = clock();

    double elapsedTime = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Time: %f secs\n", elapsedTime);

    return 0;
}
