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

void *worker_function(void *arg) {
	printf("%s\n", "Thread created");
	int extractsock = 0;
	int getreq = 0;
	int getstat = 0;
	int fail = 0;
	int filesize = 0;
	struct stat checkfile;
	char * requestedfile;
	int slash = 0;
	char *ipadd;
	int portnum = 0;
	char * getip;
	while(still_running)
	{
		pthread_mutex_lock(&work_mutex);
		while(queue_count == 0){
			printf("%s\n", "going to sleep");
			pthread_cond_wait(&work_cond, &work_mutex);
		}
		extractsock = tail->sock;
		if(tail->previous == NULL) //only one item in list
		{
			head = NULL;
			getip = tail->ip;
			ipadd = (char *) malloc(sizeof(char)*strlen(getip) + 1);
			int x = 0;
			for(; x < strlen(getip)+1; x++)
			{
				ipadd[x] = getip[x];
			}	
			free(tail);
			tail = NULL;
		}
		else {
			tail = tail->previous; //set to one before (know exists)
			portnum = tail->next->port;
			getip = tail->next->ip;
			ipadd = (char *) malloc(sizeof(char)*strlen(getip) + 1);
			int x = 0;
			for(; x < strlen(getip)+1; x++)
			{
				ipadd[x] = getip[x];
			}		
			free(tail->next);	
			tail->next = NULL;
		}
		queue_count--;
		pthread_mutex_unlock(&work_mutex);

		getreq = getrequest(extractsock, requestedfile, 1024);
		if (!getreq) {
			
			if (requestedfile[0] == '/')
			{
				requestedfile++;
				slash = 1;
			}
			getstat = stat(requestedfile, &checkfile);
			if (!getstat) {			
				filesize = checkfile.st_size;
				int datasize = 63 + sizeof(filesize) +  filesize;
				char data [datasize];
				sprintf(data, HTTP_200, (int)checkfile.st_size);
				int fd = open("main.c", O_RDONLY);
				read(fd, data+(63+sizeof(filesize)), checkfile.st_size);
				data[datasize] = '\0';
				senddata(extractsock, data, datasize);
				shutdown(extractsock, SHUT_RDWR);
				close(fd);
			}
			else {
				fail = 1;			
			}
		}
		else {
			fail = 1;		
		}
		if(fail){
			senddata(extractsock, HTTP_404, sizeof(HTTP_404));
		}	
		pthread_mutex_lock(&log_mutex);	
		FILE * log = fopen("weblog.txt", "a");
		fwrite(ipadd, strlen(ipadd), 1, log);
		char portstr[sizeof(portnum)];
		sprintf(portstr, ":%d ", portnum);
		fwrite(portstr, strlen(portstr), 1, log);
		time_t now = time(NULL);
		char * time = ctime(&now);
		fwrite(time, strlen(time), 1, log);
		fwrite(" \"GET " , 6, 1, log);		
		if (slash)
		{
			requestedfile--;
		}	
		fwrite(requestedfile, strlen(requestedfile), 1, log);
		if(fail){
			fwrite("\" 404 ", 6, 1, log);
		}		
		else {
			fwrite("\" 200 ", 6, 1, log);
		}
		char buffer[sizeof(filesize)];
		sprintf(buffer, "%d\n", filesize);
		fwrite(buffer, strlen(buffer), 1, log);
		fclose(log);
		pthread_mutex_unlock(&log_mutex);
	}
	return NULL;
}

void runserver(int numthreads, unsigned short serverport) {
    //////////////////////////////////////////////////
    // create your pool of threads here
	//pthread_t p1;
	//pthread_create(&p1,NULL, worker_function, NULL);




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
	printf("%s", "loop");
	fflush(stdout);
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
		senddata(new_sock, HTTP_404, sizeof(HTTP_404));
		shutdown(new_sock, SHUT_RDWR);
        }
		printf("%d", queue_count);
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
