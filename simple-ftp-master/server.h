/**
 * SimpleFTP server interface.
**/

#ifndef SERVER_H
#define SERVER_H

#include "siftp.h"

	/* constants */
	
		#define SERVER_SOCKET_BACKLOG	5
		#define SERVER_PASSWORD	"scp18"

//		#define CONCURRENT 1

	/* services */
	
		/**
		 * Establishes a network service on the specified port.
		 * @param	ap_socket	Storage for socket descriptor.
		 * @param	a_port	Port number in decimal.
		 */
		Boolean service_create(int *ap_socket, const int a_port);

#endif

void* clientSession(ClientArgs* args);

time_t getNow();

char * getTime(); /*funcio que retorna l' hora pel debug*/

void* threadController();

void* threadPrintStadistics();
