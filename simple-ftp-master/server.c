#include "pthread.h"
#include "service.h"
#include "server.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <semaphore.h>

#define C_MAX  5

// globals
	char g_pwd[PATH_MAX+1];
    int _sesion=0;
	pthread_t **cancelThreads;
	pthread_t *activeThreads;

	ThreadStats *stats;
	int nHilos = 1;
    int cCount = 0;
	pthread_mutex_t lockLocalStats = PTHREAD_MUTEX_INITIALIZER, lockStats = PTHREAD_MUTEX_INITIALIZER, lockThreadController = PTHREAD_MUTEX_INITIALIZER, lock_sem_New = PTHREAD_MUTEX_INITIALIZER, lock_count = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t print_cond = PTHREAD_COND_INITIALIZER;

	sem_t sem_New, sem_activeThreads;
	pthread_barrier_t printTotals, printSesion;//


	/* Estadistiques globals */
	int total_nCommands = 0;
	int total_mB_s_down = 0;
	int total_mB_s_up = 0;
	double totalTime = 0;

    time_t local_start_time;
    time_t local_start_timeC;

// funcs


void sigchld_handler(int s)
{
	while(wait(NULL) > 0);
}


void printLocalStats(){
    time_t local_end_time;
    double total_nCommandsXminut = 0;
    double mb_down_avg = 0;
    double mb_up_avg = 0;

    local_end_time = getNow();


    pthread_mutex_lock(&lockLocalStats);
    double diff = difftime(local_end_time, local_start_timeC);
    pthread_mutex_unlock(&lockLocalStats);

    if(diff > 60){
        total_nCommandsXminut = (double) ( (total_nCommands / diff) / 60);
    }else{
        total_nCommandsXminut = total_nCommands;
    }

    mb_down_avg = (double)total_mB_s_down / nHilos;
    mb_up_avg = (double)total_mB_s_up / nHilos;

    pthread_barrier_wait(&printTotals);
    pthread_barrier_init(&printSesion,NULL,2);

    printf("\n[+]===============================================");
    printf("\n[+]                    T O T A L S                ");
    printf("\n[+]===============================================");
    printf("\n[ ] Total execution time :: %.0f sec.", diff);
    printf("\n[+]+++++++++++++++++++++++++++++++++++++++++++++++");
    printf("\n[+] Stats[nCommand] %d", total_nCommands);
    printf("\n[+] Stats[nCommand /minut] %.2f", total_nCommandsXminut);
    printf("\n[+] Stats[mb/s down] %i mb/s", total_mB_s_down);
    printf("\n[+] Stats[mb/s up] %i mb/s", total_mB_s_up);
    printf("\n[+] Stats[avg mb/s down] %.2f mb/s", mb_down_avg);
    printf("\n[+] Stats[avg mb/s up] %.2f mb/s", mb_up_avg);
    printf("\n[+]===============================================");
    printf("\n");

    pthread_barrier_init(&printSesion,NULL,1);
}


/* Funcio exit when Ctr+C*/
void exitHandler(){
	/* Abortar fills */

	for(int i=0; i<_sesion; ++i){
        if(stats[i].stat == 1){
            printf("\n Thred [%d] : Closed\n", i+1);
            pthread_cancel((pthread_t)cancelThreads[i]);
        }
	}

    printLocalStats();


    pthread_mutex_destroy(&lockThreadController);
	pthread_mutex_destroy(&lock_sem_New);
	sem_destroy(&sem_New);
	sem_destroy(&sem_activeThreads);


	free(cancelThreads);
	free(stats);
	printf("\n -- Server closed --\n");
	exit(0);
}

time_t getNow(){
	time_t timeval;
	timeval = time(NULL);
	localtime(&timeval);

	return timeval;
}
char * getTime() /*funcio que retorna l' hora pel debug*/
{
	char aux[75];
	char * now = malloc(sizeof(char) * 75);
	time_t tmptime;

	tmptime = getNow();

	strftime (aux, 75, "%H:%M:%S", localtime(&tmptime));
	sprintf(now, "%s", aux);

	return now;
}


void setStat(ThreadStats *stats, int position, uint stat){
	stats[position].stat = stat;
}


void initialitzeStats(ThreadStats *stats, int size){
	for(int i=0; i<size; ++i){
		stats[i].life = 0;
		stats[i].nCommands = 0;
		stats[i].nCommandsXminut = 0;
		stats[i].nPut = 0;
		stats[i].nGet = 0;
		stats[i].mB_s_up = 0;
		stats[i].mB_s_down = 0;
		setStat(stats, i, 0);
	}
}



int firstStat(ThreadStats *stats, int size){
	for(int i=0; i<size; ++i){
		if(stats[i].stat == 0){
			return i;
		}
	}

    return -1;
}

int main(int a_argc, char **ap_argv)
{
	// variables
    int serverSocket, clientSocket;
    socklen_t clientAddrSize;
	struct sockaddr_in clientAddr;
	struct sigaction signalAction;
	pthread_attr_t attr;
	pthread_attr_t attr_threadContr;
	pthread_attr_t attr_printThreadStadistics;

	pthread_attr_init(&attr);
	pthread_attr_init(&attr_threadContr);
	pthread_attr_init(&attr_printThreadStadistics);

	// check args
		if(a_argc < 4)
		{
			printf("Usage: %s <directorio_ftp> <port number> <Max_Threads>\n", ap_argv[0]);
			return 1;
		}

		if(a_argc == 4){
			sscanf(ap_argv[3], "%d", &nHilos);
			_sesion = nHilos;
			printf("\n+[nhilos] = %d\n", nHilos);
		}

    local_start_time = getNow();
    local_start_timeC = getNow();

	sem_init(&sem_New, 0, nHilos);
	sem_init(&sem_activeThreads, 0, 0);
    pthread_barrier_init(&printTotals,NULL,1);
    pthread_barrier_init(&printSesion,NULL,1);

	pthread_t tid[nHilos];
	pthread_t tid_threadContr;
    pthread_t tid_threadPrintStadistics;

	signal(SIGINT, exitHandler);

	printf(" { %s } ", getTime());

	cancelThreads = (pthread_t**) malloc( sizeof(pthread_t) * nHilos );
	activeThreads = (pthread_t*) malloc( sizeof(pthread_t) * nHilos );
	stats = (ThreadStats*) malloc(sizeof(ThreadStats) * nHilos);

	// init vars
		realpath(ap_argv[1], g_pwd);

	// create service
		if(!service_create(&serverSocket, strtol(ap_argv[2], (char**)NULL, 10)))
		{
			fprintf(stderr, "%s: unable to create service on port: %s\n", ap_argv[0], ap_argv[2]);
			return 2;
		}



	// setup termination handler
		signalAction.sa_handler = sigchld_handler; // reap all dead processes
		sigemptyset(&signalAction.sa_mask);
		signalAction.sa_flags = SA_RESTART;

		if (sigaction(SIGCHLD, &signalAction, NULL) == -1)
		{
			perror("main(): sigaction");
			return 3;
		}

		initialitzeStats(stats, nHilos);

		if (pthread_create(&tid_threadContr, &attr_threadContr, (void *(*)(void *)) threadController, NULL) != 0) {
			perror("\nError: Hilo threads controller");
			//exit(-1);
		}

		if(pthread_create(&tid_threadPrintStadistics, &attr_printThreadStadistics, (void *(*)(void *)) threadPrintStadistics,NULL) != 0){
			perror("\nError: Hilo print thread");
			//exit(-1);
		}

	// dispatcher loop
		while(true)
		{
			sem_wait(&sem_New);
			clientAddrSize = sizeof(clientAddr);

			// wait for a client

				#ifndef NOSERVERDEBUG
					printf("\nmain(): waiting for clients...\n");
				#endif

				if((clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrSize)) != -1)
				{
					ClientArgs *tmp_args;

					tmp_args = (ClientArgs*) malloc(sizeof(ClientArgs));

					int sesion = -1;
					sesion = firstStat(stats,nHilos);

					if(sesion >= 0){
						tmp_args->a_socket = clientSocket;
						tmp_args->sesion = sesion;
						tmp_args->max = nHilos -1;
						memcpy(&tmp_args->clientAddr, &clientAddr, sizeof(struct sockaddr_in));

						/* pooint to real thread - fill threads collector*/
						cancelThreads[sesion] = &tmp_args->thread;

						/* unic client path */
						sprintf(tmp_args->g_pwd, "%s", g_pwd);

                        setStat(stats, sesion, 1);


                        if (pthread_create(&tid[sesion], &attr, (void *(*)(void *)) clientSession, (void *)tmp_args) != 0) {
							setStat(stats, sesion, 0);
							perror("\nError: Hilo cliente");
								//exit(-1);
						}
					}else{

					}

				}
				else
					perror("main()");
		}

	// destroy service
		close(serverSocket);

	return 0;
}

void* clientSession(ClientArgs* args){
#ifndef NOSERVERDEBUG
    printf("\nmain(): got client connection [addr=%s,port=%d] --> %d\n", inet_ntoa(args->clientAddr.sin_addr), ntohs(args->clientAddr.sin_port), args->sesion);
#endif
    fd_set rfds;
    time_t start_time;
    time_t end_time;

    args->thread = pthread_self();

    // dispatch job

    // service the client
    if(session_create(args->a_socket, args->sesion))
    {
		pthread_mutex_lock(&lockStats);
		stats[args->sesion].stat = 1;
		pthread_mutex_unlock(&lockStats);

        start_time = getNow();

        service_loop(args);
    }

    session_destroy(args->a_socket);
    close(args->a_socket); // parent doesn't need this socket
    end_time = getNow();

    double diff = difftime(end_time, start_time);

	pthread_mutex_lock(&lockStats);
	memcpy(&stats[args->sesion].life, &diff, sizeof(double));

    setStat(stats, args->sesion, 0);

	if(stats[args->sesion].life > 60){
        stats[args->sesion].nCommandsXminut = (double) (stats[args->sesion].nCommands / (stats[args->sesion].life / 60));
    }else{
        stats[args->sesion].nCommandsXminut = stats[args->sesion].nCommands;
    }
	pthread_mutex_unlock(&lockStats);


    printf("\n -> Client sesion %d disconnected.", args->sesion);


    pthread_barrier_wait(&printSesion);
    pthread_barrier_init(&printTotals,NULL,2);

	pthread_mutex_lock(&lockStats);
	printf("\n -> Client execution time :: %.0f sec.", stats[args->sesion].life);
    printf("\n");
    printf("\n[+] +++++++++++++++++++");
    printf("\n[+] Stats[nCommand] %d", stats[args->sesion].nCommands);
    printf("\n[+] Stats[nCommand /minut] %.2f", stats[args->sesion].nCommandsXminut);

    double mb_down_avg = 0;
	if(stats[args->sesion].life > 0) {
		mb_down_avg = (double) (stats[args->sesion].mB_s_down / stats[args->sesion].life);
	}else{
		mb_down_avg = stats[args->sesion].mB_s_down;
	}

	double mb_up_avg = 0;

	if(stats[args->sesion].life > 0){
		mb_up_avg= (double)(stats[args->sesion].mB_s_up/stats[args->sesion].life);
	}else{

		mb_up_avg= stats[args->sesion].mB_s_up;
	}

	printf("\n[+] Stats[mb/s down] %i mb/s", stats[args->sesion].mB_s_down);
	printf("\n[+] Stats[mb/s up] %i mb/s", stats[args->sesion].mB_s_up);
	printf("\n[+] Stats[avg mb/s down] %.2f mb/s", mb_down_avg);
	printf("\n[+] Stats[avg mb/s up] %.2f mb/s", mb_up_avg);
    printf("\n[+] +++++++++++++++++++\n");

    pthread_barrier_init(&printTotals,NULL,1);


	//Sumar estadistiques
	pthread_mutex_unlock(&lockLocalStats);
	totalTime += stats[args->sesion].life;
	pthread_mutex_unlock(&lockLocalStats);

	/* Update stats table */
	stats[args->sesion].life = 0;
	stats[args->sesion].nCommands = 0;
	stats[args->sesion].nCommandsXminut = 0;
	stats[args->sesion].nPut = 0;
	stats[args->sesion].nGet = 0;
	stats[args->sesion].mB_s_up = 0;
	stats[args->sesion].mB_s_down = 0;
	setStat(stats, args->sesion, 0);
	pthread_mutex_unlock(&lockStats);

	activeThreads[args->sesion] = pthread_self();

	sem_post(&sem_activeThreads);

	sem_post(&sem_New);

    /* Clean socket file descritors and free args */
    FD_ZERO(&rfds);
    FD_SET(args->a_socket,&rfds);

    free(args);

    pthread_exit(0);
}


void* threadPrintStadistics() {

	while(1){
        //pthread_barrier_wait(&CBarrier);

        pthread_mutex_lock(&lock_count);
        while(cCount < C_MAX){
            pthread_cond_wait(&print_cond, &lock_count);
        }

        printLocalStats();

        cCount = 0;
        pthread_mutex_unlock(&lock_count);
    }
}
void* threadController() {
    while(1){
        sem_wait(&sem_activeThreads);
		//barrier

		for(int i = 0; i < nHilos; ++i){
			if(activeThreads[i] != (pthread_t)NULL){
				pthread_cancel(activeThreads[i]);
                pthread_t *tmp = NULL;
                memcpy(&activeThreads[i], &tmp, sizeof(pthread_t));
                break;
			}
		}
    }

}

Boolean service_create(int *ap_socket, const int a_port)
{
	// variables
		struct sockaddr_in serverAddr;
		
		#ifdef _PLATFORM_SOLARIS
			char yes='1';
		#else
			int yes=1;
		#endif
		
	// create address
		memset(&serverAddr, 0, sizeof(serverAddr));
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddr.sin_port = htons(a_port);
		
	// create socket
		if((*ap_socket = socket(serverAddr.sin_family, SOCK_STREAM, 0)) < 0)
		{
			perror("service_create(): create socket");
			return false;
		}
		
	// set options
		if(setsockopt(*ap_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
		{
			perror("service_create(): socket opts");
			return false;
		}
	
	// bind socket
		if(bind(*ap_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
		{
			perror("service_create(): bind socket");
			close(*ap_socket);
			return false;
		}
		
	// listen to socket
		if(listen(*ap_socket, SERVER_SOCKET_BACKLOG) < 0)
		{
			perror("service_create(): listen socket");
			close(*ap_socket);
			return false;
		}
	
	return true;
}


Boolean session_create(const int a_socket, int sesion)
{
	// variables
		Message msgOut, msgIn;
		
	// init vars
		Message_clear(&msgOut);
		Message_clear(&msgIn);

	// session challenge dialogue
	
		// client: greeting
			if(!siftp_recv(a_socket, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_SESSION_BEGIN))
			{
				fprintf(stderr, "[%d] service_create(): session not requested by client.\n",sesion);
				return false;
			}
			
		// server: identify
		// client: username
			Message_setType(&msgOut, SIFTP_VERBS_IDENTIFY);
			
			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_USERNAME))
			{
				fprintf(stderr, "[%d] service_create(): username not specified by client.\n",sesion);
				return false;
			}
		
		// server: accept|deny
		// client: password
			Message_setType(&msgOut, SIFTP_VERBS_ACCEPTED); //XXX check username... not required for this project
			
			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_PASSWORD))
			{
				fprintf(stderr, "[%d] service_create(): password not specified by client.\n",sesion);
				return false;
			}
		
		// server: accept|deny
			if(Message_hasValue(&msgIn, SERVER_PASSWORD))
			{
				Message_setType(&msgOut, SIFTP_VERBS_ACCEPTED);
				siftp_send(a_socket, &msgOut);
			}
			else
			{
				Message_setType(&msgOut, SIFTP_VERBS_DENIED);
				siftp_send(a_socket, &msgOut);
				setStat(stats, sesion, 0);
				fprintf(stderr, "[%d] service_create(): client password rejected.\n",sesion);
				return false;
			}
		
		// session now established
			#ifndef NOSERVERDEBUG
				printf("[%d] session_create(): success\n",sesion);
			#endif
			
	return true;
}

void service_loop(ClientArgs *args)
{
	// variables
	Message msg;

	String *p_argv;
	int argc;

	// init vars
	Message_clear(&msg);

	while(siftp_recv(args->a_socket, &msg)) // await request
	{
		if(Message_hasType(&msg, SIFTP_VERBS_SESSION_END) || Message_hasType(&msg, ""))
			break;

		else
		{
#ifndef NOSERVERDEBUG
			printf("[%d] service_loop(): got command '%s'\n",args->sesion, Message_getValue(&msg));
#endif
			//sprintf(&args->stats->nCommands, "%d", args->stats->nCommands+1);


			// parse request
			if((p_argv = service_parseArgs(Message_getValue(&msg), &argc)) == NULL || argc <= 0)
			{
				service_freeArgs(p_argv, argc);

				if(!service_sendStatus(args->a_socket, false)) // send negative ack
					break;

				continue;
			}


			// handle request
			if(!service_handleCmd(args, p_argv, argc, args->sesion))
				service_sendStatus(args->a_socket, false); // send negative ack upon fail


			// clean up
			service_freeArgs(p_argv, argc);
			p_argv = NULL;

			/*
            if(!service_handleCmd(a_socket, Message_getValue(&msg))) // send failure notification
            {
                Message_setType(&msg, SIFTP_VERBS_ABORT);

                if(!siftp_send(a_socket, &msg))
                {
                    fprintf(stderr, "service_loop(): unable to send faliure notice.\n");
                    break;
                }
            }
            */
		}
	}
}


void checkPrintC(){
    pthread_mutex_lock(&lock_count);
    ++cCount;

	pthread_mutex_lock(&lockLocalStats);
	++total_nCommands;
	pthread_mutex_unlock(&lockLocalStats);

    if(cCount == C_MAX){
        pthread_cond_broadcast(&print_cond);
    }
    pthread_mutex_unlock(&lock_count);
}


Boolean service_handleCmd(ClientArgs *args, const String *ap_argv, const int a_argc, int sesion)
{
	// variables
		Message msg;
		
		String dataBuf;
		int dataBufLen;
		
		Boolean tempStatus = false;
		
	// init variables
		Message_clear(&msg);

	// handle commands
	if(strcmp(ap_argv[0], "ls") == 0)
	{
		if((dataBuf = service_readDir(args->g_pwd, &dataBufLen)) != NULL)
		{
            checkPrintC();

            pthread_mutex_lock(&lockStats);
			++stats[args->sesion].nCommands;
			pthread_mutex_unlock(&lockStats);

			// transmit data
				if(service_sendStatus(args->a_socket, true))
					tempStatus = siftp_sendData(args->a_socket, dataBuf, dataBufLen);
				
				#ifndef NOSERVERDEBUG
					printf("[%d] ls(): status=%d\n",sesion, tempStatus);
				#endif

			// clean up
				free(dataBuf);
			return tempStatus;
		}
	}
	
	else if(strcmp(ap_argv[0], "pwd") == 0)
	{
        checkPrintC();

		pthread_mutex_lock(&lockStats);
		++stats[args->sesion].nCommands;
		pthread_mutex_unlock(&lockStats);

		if(service_sendStatus(args->a_socket, true)) {
			return siftp_sendData(args->a_socket, args->g_pwd, strlen(args->g_pwd));
		}
	}
	
	else if(strcmp(ap_argv[0], "cd") == 0 && a_argc > 1)
	{
        checkPrintC();

        pthread_mutex_lock(&lockStats);
		++stats[args->sesion].nCommands;
		pthread_mutex_unlock(&lockStats);

		return service_sendStatus(args->a_socket, service_handleCmd_chdir(args->g_pwd, ap_argv[1]));
	}
	
	else if(strcmp(ap_argv[0], "get") == 0 && a_argc > 1)
	{
        checkPrintC();

        pthread_mutex_lock(&lockStats);
		++stats[args->sesion].nCommands;
		pthread_mutex_unlock(&lockStats);

		char srcPath[PATH_MAX+1];
		
		// determine absolute path
		if(service_getAbsolutePath(args->g_pwd, ap_argv[1], srcPath))
		{
			// check read perms & file type
			if(service_permTest(srcPath, SERVICE_PERMS_READ_TEST) && service_statTest(srcPath, S_IFMT, S_IFREG))
			{
				// read file
				if((dataBuf = service_readFile(srcPath, &dataBufLen)) != NULL)
				{
					if(service_sendStatus(args->a_socket, true))
					{
						pthread_mutex_lock(&lockStats);
						++stats[args->sesion].nGet;
						pthread_mutex_unlock(&lockStats);

                        // send file
						tempStatus = siftp_sendData(args->a_socket, dataBuf, dataBufLen);

						pthread_mutex_lock(&lockStats);
						stats[args->sesion].mB_s_down += dataBufLen;
						pthread_mutex_unlock(&lockStats);

						pthread_mutex_lock(&lockLocalStats);
						total_mB_s_down += dataBufLen;
						pthread_mutex_unlock(&lockLocalStats);


						#ifndef NOSERVERDEBUG
							printf("[%d] get(): file sent %s.\n",sesion, tempStatus ? "OK" : "FAILED");
						#endif
					}
					#ifndef NOSERVERDEBUG
					else
						printf("[%d] get(): remote host didn't get status ACK.\n",sesion);
					#endif
					
					free(dataBuf);
				}
				#ifndef NOSERVERDEBUG
				else
					printf("[%d]get(): file reading failed.\n",sesion);
				#endif
			}
			#ifndef NOSERVERDEBUG
			else
				printf("[%d]get(): don't have read permissions.\n",sesion);
			#endif
		}
		#ifndef NOSERVERDEBUG
		else
			printf("[%d] get(): absolute path determining failed.\n",sesion);
		#endif
			
		return tempStatus;
	}
	
	else if(strcmp(ap_argv[0], "put") == 0 && a_argc > 1)
	{
		char dstPath[PATH_MAX+1];

        checkPrintC();

        pthread_mutex_lock(&lockStats);
		++stats[args->sesion].nCommands;
		pthread_mutex_unlock(&lockStats);

		// determine destination file path
		if(service_getAbsolutePath(args->g_pwd, ap_argv[1], dstPath))
		{
			// check write perms & file type
			if(service_permTest(dstPath, SERVICE_PERMS_WRITE_TEST) && service_statTest(dstPath, S_IFMT, S_IFREG))
			{
				// send primary ack: file perms OK
				if(service_sendStatus(args->a_socket, true))
				{
					pthread_mutex_lock(&lockStats);
					++stats[args->sesion].nPut;
					pthread_mutex_unlock(&lockStats);

                    // receive file
					if((dataBuf = siftp_recvData(args->a_socket, &dataBufLen)) != NULL)
					{
						#ifndef NOSERVERDEBUG
							printf("[%d] put(): about to write to file '%s'\n",sesion, dstPath);
						#endif

						tempStatus = service_writeFile(dstPath, dataBuf, dataBufLen);

						pthread_mutex_lock(&lockStats);
						stats[args->sesion].mB_s_up += dataBufLen;
						pthread_mutex_unlock(&lockStats);

						pthread_mutex_lock(&lockLocalStats);
						total_mB_s_up += dataBufLen;
						pthread_mutex_unlock(&lockLocalStats);


						free(dataBuf);
						
						#ifndef NOSERVERDEBUG
							printf("[%d] put(): file writing %s.\n", sesion, tempStatus ? "OK" : "FAILED");
						#endif
						
						// send secondary ack: file was written OK
						if(tempStatus)
						{
							return service_sendStatus(args->a_socket, true);
						}
					}
					#ifndef NOSERVERDEBUG
					else
						printf("[%d] put(): getting of remote file failed.\n",sesion);
					#endif
				}
				#ifndef NOSERVERDEBUG
				else
					printf("[%d] put(): remote host didn't get status ACK.\n",sesion);
				#endif
			}
			#ifndef NOSERVERDEBUG
			else
				printf("[%d] put(): don't have write permissions.\n",sesion);
			#endif
		}
		#ifndef NOSERVERDEBUG
		else
			printf("[%d] put(): absolute path determining failed.\n",sesion);
		#endif
	}
	
	return false;
}
