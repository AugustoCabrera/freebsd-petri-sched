#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/metadata_payloads.h>

__PAYLOAD__;

int main(int argc, char *argv[]) {

	printf("\n******** metadata codeinsertion finished ********\n");
	printf("\n********   now sleep during 60 seconds   ********\n");
	sleep(60);

	return 0;
}