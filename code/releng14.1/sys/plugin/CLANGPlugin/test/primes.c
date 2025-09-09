#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <sys/metadata_payloads.h>

__PAYLOAD__;

bool isPrime(int num) {
   if (num <= 1) 
      return false;

   for (int i = 2; i * i <= num; ++i) {
      if (num % i == 0)
          return false;
   }

   return true;
}

/*!
 *  \param argv[1] Highest number to test for primality
 */
int main(int argc, char** argv) {

   long MAXIMUM;
   long total_number_of_primes = 0;
   unsigned char is_prime;

   if (argc != 2) {
      printf("Usage: ./prime ");
      printf("[highest number to test for primality]\n");
      printf("Please try again.\n");
      exit(1);
   }

   if ((MAXIMUM = atol(argv[1])) <= 0) {
      printf("Error: Invalid argument for highest number to test for primality. Please try again.\n");
      exit(1);
   }

   clock_t start = clock();

   for (int i = 2; i <= MAXIMUM; ++i) {
      if (isPrime(i))
         total_number_of_primes++;
   }

   printf("Total primes up to %ld: %ld\n", MAXIMUM, total_number_of_primes);

   clock_t end = clock();

   double elapsedTime = ((double)(end - start)) / CLOCKS_PER_SEC;

   printf("Time: %f secs\n", elapsedTime);

   return 0;
}