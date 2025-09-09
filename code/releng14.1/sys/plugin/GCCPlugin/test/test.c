#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/metadata_payloads.h>

__PAYLOAD__;

 int main(int argc, char *argv[]) {

 	printf("\n******** metadata codeinsertion finished ********\n");
 	printf("\n********   now sleep during 60 seconds   ********\n");
	sleep(120);

 	return 0;
 }

//////////////


/*
bool is_prime(int n) {
    if (n <= 1) return false;
    for (int i = 2; i*i <= n; ++i)
        if (n % i == 0) return false;
    return true;
}

int main() {
    int count = 0;
    for (int i = 2; i < 10000000; ++i) {
        if (is_prime(i)) count++;
    }
    printf("Found %d primes\n", count);
    return 0;
} */

