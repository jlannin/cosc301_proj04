/*
Justin Lannin and Adam Becker
11/20/13
COSC 301

Professor Sommers

We both worked on all aspects of this project.  Work was distributed very fairly.

*/


#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include "network.h"

struct work_queue_item {
int sock;
struct work_queue_item* next;
struct work_queue_item* previous;
int port;
char * ip;

};

// global variable; can't be avoided because
// of asynchronous signal interaction
struct work_queue_item *head = NULL;
struct work_queue_item *tail = NULL;
int queue_count = 0;
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

int still_running = TRUE;
void signal_handler(int sig) {
    still_running = FALSE;
}


void usage(const char *progname) {
    fprintf(stderr, "usage: %s [-p port] [-t numthreads]\n", progname);
    fprintf(stderr, "\tport number defaults to 3000 if not specified.\n");
    fprintf(stderr, "\tnumber of threads is 1 by default.\n");
    exit(0);
}

//returns number of digits of x
int intsize (int x) {
	int count = 1;
	while (x > 9){
		x = x / 10;
		count++;
	}
	return count;
}

//processes data and updates the log
void processandlog(int sock, char * ipadd, int portnum)
{
		struct stat checkfile;
		int getstat = 0;
		int fd = 0;
		int sendfail = 0;
		int filesize = 0;
		int sendreturn = 0;
		char requestedfile [1024] = "";
		int fail = 0;
		int getreq = getrequest(sock, requestedfile, 1024);
		if (!getreq) {
			if (requestedfile[0] == '/')
			{
				getstat = stat(&(requestedfile[1]), &checkfile);
			}
			else {
				getstat = stat(requestedfile, &checkfile);
			}
			if (!getstat) {			
				filesize = checkfile.st_size;
				int datasize = 63 + intsize(filesize) + filesize;
				char data [datasize];
				sprintf(data, HTTP_200, (int)checkfile.st_size);
				
				if (requestedfile[0] == '/')
				{	
					fd = open(&(requestedfile[1]), O_RDONLY);
				}
				else {
					fd = open(requestedfile, O_RDONLY);
				}				
				read(fd, &(data[(63 + intsize(filesize))]), checkfile.st_size);			
				data[datasize] = '\0';
				sendreturn = senddata(sock, data, datasize);
				if (sendreturn == -1)
				{
					printf("%s", "Data sending failed! So sad");
					sendfail = 1;
				}
				shutdown(sock, SHUT_RDWR);
				close(fd);
			}
			else {
				fail = 1;			
			}
		}
		else {
			fail = 1;	//getrequest fails means that we won't ever find the file, return file not found	
		}
		if(fail){
			sendreturn = senddata(sock, HTTP_404, strlen(HTTP_404));
			if (sendreturn == -1)
			{
					printf("%s", "Data sending failed! So sad");
					sendfail = 1;
			}
			shutdown(sock, SHUT_RDWR);		
		}
		if(!sendfail)
		{
			pthread_mutex_lock(&log_mutex);	
			FILE * log = fopen("weblog.txt", "a");
			fwrite(ipadd, strlen(ipadd), 1, log);
			char portstr[intsize(portnum)];
			sprintf(portstr, ":%d ", portnum);
			fwrite(portstr, strlen(portstr), 1, log);
			time_t now = time(NULL);
			char * time = ctime(&now);
			fwrite(time, strlen(time)-1, 1, log);
			fwrite(" \"GET " , 6, 1, log);
			fwrite(requestedfile, strlen(requestedfile), 1, log);
			if(fail){
				fwrite("\" 404 ", 6, 1, log);
			}		
			else {
				fwrite("\" 200 ", 6, 1, log);
			}
			char buffer[intsize(sendreturn)];
			sprintf(buffer, "%d\n", sendreturn);
			fwrite(buffer, strlen(buffer), 1, log);
			fclose(log);
			pthread_mutex_unlock(&log_mutex);
		}
		else
		{	//since we want to write to log for every request, it makes sense to keep track of senddata failures
			pthread_mutex_lock(&log_mutex);	
			FILE * log = fopen("weblog.txt", "a");
			fwrite("Senddata failed\n", 16, 1, log);
			fclose(log);
			pthread_mutex_unlock(&log_mutex);
		}
}

//handles worker threads
//workers get next item off of queue
//and call process and log
void *worker_function(void *arg) {
	printf("%s\n", "Thread created");
	int extractsock = 0;
	char *ipadd;
	int portnum = 0;
	char * getip;
	while(1)
	{
		pthread_mutex_lock(&work_mutex);
		while(queue_count == 0){
			pthread_cond_wait(&work_cond, &work_mutex);
		}
		if(queue_count == -1){
			pthread_mutex_unlock(&work_mutex);
			break;
		}
		extractsock = tail->sock;
		getip = tail->ip;
		portnum = tail->port;
		ipadd = (char *) malloc(sizeof(char)*strlen(getip) + 1);
		int x = 0;
		for(; x < strlen(getip)+1; x++)
		{
			ipadd[x] = getip[x];
		}
		if(tail->previous == NULL) //only one item in list
		{
			head = NULL;	
			free(tail);
			tail = NULL;
		}
		else {
			tail = tail->previous; //set to one before (know exists)
			free(tail->next);	
			tail->next = NULL;
		}
		queue_count--;
		pthread_mutex_unlock(&work_mutex);
		processandlog(extractsock, ipadd, portnum);
		free(ipadd);
	}
	return NULL;
}

void runserver(int numthreads, unsigned short serverport) {
    //////////////////////////////////////////////////
    // create your pool of threads here
	//pthread_t p1;
	//
	pthread_t threadarray [numthreads];
	int x = 0;
	for(; x < numthreads; x++)
	{
		pthread_create(&(threadarray[x]),NULL, worker_function, NULL);
	}


    //////////////////////////////////////////////////
    
    
    int main_socket = prepare_server_socket(serverport);
    if (main_socket < 0) {
        exit(-1);
    }
    signal(SIGINT, signal_handler);

    struct sockaddr_in client_address;
    socklen_t addr_len;

    fprintf(stderr, "Server listening on port %d.  Going into request loop.\n", serverport);
    while (still_running) {
	//printf("%s", "loop");
	//fflush(stdout);
        struct pollfd pfd = {main_socket, POLLIN};
        int prv = poll(&pfd, 1, 10000);

        if (prv == 0) {
            continue;
        } else if (prv < 0) {
            PRINT_ERROR("poll");
            still_running = FALSE;
            continue;
        }
        
        addr_len = sizeof(client_address);
        memset(&client_address, 0, addr_len);

        int new_sock = accept(main_socket, (struct sockaddr *)&client_address, &addr_len);
        if (new_sock > 0) {
            
            time_t now = time(NULL);
            fprintf(stderr, "Got connection from %s:%d at %s\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port), ctime(&now));

           ////////////////////////////////////////////////////////
           /* You got a new connection.  Hand the connection off
            * to one of the threads in the pool to process the
            * request.
            *
            * Don't forget to close the socket (in the worker thread)
            * when you're done.
            */
           ////////////////////////////////////////////////////////
		struct work_queue_item * newnode = (struct work_queue_item *) malloc(sizeof(struct work_queue_item));
		newnode->sock = new_sock;
		pthread_mutex_lock(&work_mutex);
		if (head != NULL) {
			head->previous = newnode;
		}
		else {
			tail = newnode;	
		}
		newnode->next = head;
		head = newnode;
		newnode->previous = NULL;
		newnode->port = ntohs(client_address.sin_port);
		newnode->ip =inet_ntoa(client_address.sin_addr);
		queue_count++;
		pthread_cond_signal(&work_cond);
		pthread_mutex_unlock(&work_mutex);
		senddata(new_sock, HTTP_200, strlen(HTTP_200));
        }
    }

    
    while(queue_count > 0); //spin wait for threads to finish
    queue_count = -1; //when signal all threads should break and be free!
    pthread_cond_broadcast(&work_cond);
    x = 0;
    for(; x < numthreads; x++)
    {
		
		pthread_join(threadarray[x], NULL);		
    }
    fprintf(stderr, "Server shutting down.\n");
    close(main_socket);
}




int main(int argc, char **argv) {
    unsigned short port = 3000;
    int num_threads = 1;
    int c;
    while (-1 != (c = getopt(argc, argv, "hp:t:"))) {
        switch(c) {
            case 'p':
                port = atoi(optarg);
                if (port < 1024) {
                    usage(argv[0]);
                }
                break;

            case 't':
                num_threads = atoi(optarg);
                if (num_threads < 1) {
                    usage(argv[0]);
                }
                break;
            case 'h':
            default:
                usage(argv[0]);
                break;
        }
    }

    runserver(num_threads, port);
    
    fprintf(stderr, "Server done.\n");
    exit(0);
}
