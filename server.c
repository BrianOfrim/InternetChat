/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   This version runs forever, forking off a separate 
   process for each connection
*/
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#define READ 0
#define WRITE 1
#define MAX_LOG 1000
#define MAX_NUM_CLIENT 100

const char FIRST_BYTE = 0xCF; 
const char SECOND_BYTE = 0xA7; 
const char MESSAGE_BYTE = 0x00;
const char JOIN_BYTE = 0x01;
const char LEAVE_BYTE = 0x02;
FILE *fp;

int masterServerPid = -1;

struct node {
   char name[256];
   int pid;
   int fd[2];
};

struct LOG_LINE {
    char line[1024];
};

volatile struct node *head;
volatile struct LOG_LINE * LOG_CONTENT;

void daemonize() {
    pid_t pid = 0;
    pid_t sid = 0;
    int i = 0;
    pid = fork();

    if (pid < 0)
    {
        printf("fork failed!\n");
        exit(1);
    }

    if (pid > 0)
    {
    	// in the parent
       printf("pid of child process %d \n", pid);
       exit(0); 
    }

    umask(0);

    // create new process group -- don't want to look like an orphan
    sid = setsid();
    if(sid < 0)
    {
    	printf("cannot create new process group");
        exit(1);
    }
    
    /* Change the current working directory */ 
    if ((chdir("/")) < 0) {
      printf("Could not change working directory to /\n");
      exit(1);
    }		
	
	// close standard fds
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void check_error(int n, const char *msg)
{
    if (n < 0) {
        perror(msg);
        exit(0);
    }
}

void check_error_write(int n, const char *msg, int fd)
{
    if (n < 0) {
        perror(msg);
        close(fd);
        exit(0);
    }
}

void check_error_read(int n, const char *msg, int fd)
{
    if (n <= 0) {
        if (n == 0) {
            // connection closed
        } else {
            perror("read: ");
            perror(msg);
        }
        close(fd); // bye!
        exit(0);
    }
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the zero-terminator
    //in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

void print_clients(int sock) {
    int n = 0;
    uint16_t num_clients = get_num_client();
    uint16_t num_clientsNBO = htons(num_clients);
    n = write(sock, &num_clientsNBO, sizeof num_clientsNBO);
    check_error_write(n,"Error on write: ",sock);
    
    int i = 0;
    for (i = 0; i<MAX_NUM_CLIENT; i ++) {
        if (head[i].pid != -1) {
            uint8_t nameLength = strlen(head[i].name);
            n = write(sock,&nameLength,sizeof(nameLength));
            check_error_write(n,"Error on write: ",sock);
            n = write(sock,head[i].name, (int) nameLength);
            check_error_write(n,"Error on write: ",sock);
        }
   }
}

void write_log(char * line) {
    int j;
    for (j = 0; j < MAX_LOG; j++) {
        if (strcmp(LOG_CONTENT[j].line, "nothing") == 0) {
            strcpy(LOG_CONTENT[j].line, line);
            break;
        }
    }
}

void inform_others_of_client_leave(struct node leavingClient){
    uint8_t lengthOfClientName = strlen(leavingClient.name);

    // Write to log
    char *line = concat(leavingClient.name, " client left the chat room!\n");
    write_log(line);

   // Inform the other clients that a new user has joined
   int n = 0;
   int i;
   for(i = 0;i < MAX_NUM_CLIENT; i++){
       if (head[i].pid != -1 && head[i].pid  != leavingClient.pid){
        // send the join header byte
        n = write(head[i].fd[WRITE], &LEAVE_BYTE , sizeof LEAVE_BYTE);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
        // send the lenght of the new clients name
        n = write(head[i].fd[WRITE], &lengthOfClientName , sizeof lengthOfClientName);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
        // send the new clients name
        n = write(head[i].fd[WRITE] , leavingClient.name , (int) lengthOfClientName);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
       }
   }
   return;
}

void inform_others_of_client_join(struct node newClient){
    uint8_t lengthOfClientName = strlen(newClient.name);
    //uint16_t lengthOfClientNameNBO = htons(lengthOfClientName);

    char *line = concat(newClient.name, " client join the chat room!\n");
    write_log(line);

   // Inform the other clients that a new user has joined
   int n = 0;
   int i;
   for(i = 0;i < MAX_NUM_CLIENT; i++){
       if (head[i].pid != -1 && head[i].pid  != newClient.pid){
        // send the join header byte
        n = write(head[i].fd[WRITE], &JOIN_BYTE , sizeof JOIN_BYTE);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
        // send the lenght of the new clients name
        n = write(head[i].fd[WRITE], &lengthOfClientName , sizeof lengthOfClientName);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
        // send the new clients name
        n = write(head[i].fd[WRITE] , newClient.name , (int) lengthOfClientName);
        check_error_write(n,"Error on write: ",head[i].fd[WRITE]);
       }
   }
   return;
}

// clean up removed client
void remove_client(int pid) {
    int i;
    for ( i = 0; i< MAX_NUM_CLIENT; i++){
        if(head[i].pid == pid) {
            inform_others_of_client_leave(head[i]);
            head[i].pid = -1;
            strcpy(head[i].name , "no_name");
            break;
        }
    }
    return;
}

void sigchld_handler(int signum)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) != -1) {
        remove_client(pid);
        break;
    }
}

void sigterm_handler(int signum) {
        int n;
        for (n = 0; n < MAX_LOG; n++) {
            if (strcmp(LOG_CONTENT[n].line, "nothing") != 0) {
                fputs(LOG_CONTENT[n].line, fp);
            }
        }
        fputs("Terminating...", fp);

        fclose(fp);

    // Terminate the server
    exit(0);
}

int get_num_client () {
    int count = 0;
    int i;
    for (i = 0; i<MAX_NUM_CLIENT; i ++) {
       if(head[i].pid != -1) 
            count ++;
   }
   return count;
}

/******** handshake_protocol() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/
void handshake_protocol (int sock)
{

   int i;
   int n;
   int selectReturn;
   char buffer[256];
   char tmpbuffer[256];
   struct timeval tv;
   tv.tv_sec = 30; // kill connnection if no activity for 30s
   tv.tv_usec = 0;
   uint16_t tmpByteCount = 0;
   uint8_t tmpNameByteCount = 0;
   struct node current_process;


   // send first confirmation bit
   n = write(sock, &FIRST_BYTE, sizeof(FIRST_BYTE));
   check_error_write(n, "ERROR writing to socket", sock);

   // send second confirmation bit
   n = write(sock, &SECOND_BYTE, sizeof(SECOND_BYTE));
   check_error_write(n, "ERROR writing to socket", sock);

    print_clients(sock); // send clinets connected to client

    // get client name
   int pid = getpid();

   bzero(buffer,256);
   uint8_t lengthOfNewClientName;
   
   n = read(sock, &lengthOfNewClientName, sizeof(lengthOfNewClientName));
   check_error_read(n, "Error reading from socket", sock);

   //read the name of the new client
   tmpByteCount = 0;
    while(tmpNameByteCount < lengthOfNewClientName){
        bzero(tmpbuffer,256);
        n = read(sock, tmpbuffer , (int) (lengthOfNewClientName - tmpNameByteCount));
        check_error_read(n, "ERROR reading from socket",sock);
        strncat(buffer, tmpbuffer, strlen(tmpbuffer));
        tmpNameByteCount += n; // increment byte count
    }

    printf("The name of the new clinet: %s \n", buffer);
   // Check if the client name is unique
   int j = 0;
   for(j = 0; j < MAX_NUM_CLIENT; j++ ){
       if(head[j].pid != -1 && (strcmp(head[j].name, buffer) == 0)){
           // the new client name is not unique terminate connection
           //write to log file
           char *line = concat(buffer, " is non-unique client name\n");
           int x;
           for (x = 0; x < MAX_LOG; x++) {
               if (strcmp(LOG_CONTENT[x].line, "nothing") == 0) {
                   strcpy(LOG_CONTENT[x].line, line);
                   break;
               }
           } 
           close(sock);
           return;
       }
   }
   
   // add info to client tracker
   int k;
   for (k = 0; k<MAX_NUM_CLIENT; k ++) {
       if (head[k].pid == -1) {
           head[k].pid = pid;
           strcpy(head[k].name , buffer);
           break;
       }
   }


   fd_set connset, readfds;
   FD_ZERO(&connset);

   int i_1;
   for (i_1 = 0; i_1 < MAX_NUM_CLIENT; i_1++) {
       // set the current process
        if (head[i_1].pid == pid) {
            current_process = head[i_1]; 
            break;
        }
    }

   FD_SET(sock, &connset); /* add sockfd to connset */
   FD_SET(current_process.fd[READ], &connset); /* read from pipe for this process */
   inform_others_of_client_join(current_process); // let the others know that a new client has entered

    while (1) {
        readfds = connset;
        tv.tv_sec = 30; // kill connnection if no activity for 30s
        tv.tv_usec = 0;
        if((selectReturn = select(FD_SETSIZE, &readfds, NULL, NULL, &tv)) < 0) {
            fprintf(stdout, "select() error code: %d\n",selectReturn);
            perror("select: ");
            exit(0);
        }       

        /* select returned
        * check which socket is set
        */

        //-----------------------------------------------------------
        // communication from the client
        //-----------------------------------------------------------
        if (FD_ISSET(sock, &readfds)) {
            // Read from socket
            bzero(buffer,256);
            // read the number of bytes were sent
            // TODO: implement seperate case for when length is zero (Keep Alive Message)
            uint16_t messageLengthNBO;
            n = read(sock, &messageLengthNBO, sizeof (messageLengthNBO));
            check_error_read(n, "ERROR reading from socket",sock);
            
            uint16_t messageLength = ntohs(messageLengthNBO); // convert back to normal
            check_error(n, "ERROR reading from socket");

            uint16_t byteCount = 0;
            // read untill the entire message has arrived
            char messageBuff[256];

            if(messageLength == 0){
                // Just a keep awake message continue 
                continue;
            }

            //read the actual message
            while(byteCount < messageLength){
                bzero(messageBuff,256);
                n = read(sock, messageBuff , messageLength - byteCount);
                check_error_read(n, "ERROR reading from socket",sock);
                strncat(buffer, messageBuff, strlen(messageBuff));
                byteCount += n; // increment byte count
            }
            
            printf("message: %s \n", buffer);
            printf("\n");

            // Write to every pipes
            for (i = 0; i<MAX_NUM_CLIENT; i++) {
                if (head[i].pid != -1){
                    // message send protocal
                    //0x00 length string messagelength message

                    //sent header byte
                    n = write(head[i].fd[WRITE], &MESSAGE_BYTE , sizeof MESSAGE_BYTE);
                    check_error_write(n,"Error writing to pipe",head[i].fd[WRITE]);

                    //sent length of sender
                    uint8_t lengthOfName = strlen(current_process.name);

                    //uint16_t lengthOfNameNBO = htons(lengthOfName);
                    n = write(head[i].fd[WRITE], &lengthOfName , sizeof lengthOfName);
                    check_error_write(n,"Error writing to pipe",head[i].fd[WRITE]);

                    //write sender Name
                    n = write(head[i].fd[WRITE], current_process.name , (int) lengthOfName);
                    check_error_write(n,"Error writing to pipe",head[i].fd[WRITE]);

                    //write message length
                    n = write(head[i].fd[WRITE], &messageLengthNBO, sizeof messageLengthNBO);
                    check_error_write(n,"Error writing to pipe",head[i].fd[WRITE]);

                    //write message 
                    n = write(head[i].fd[WRITE], buffer , messageLength);
                    check_error_write(n,"Error writing to pipe",head[i].fd[WRITE]);
                }
            }
        //-----------------------------------------------------------
        // communication from the another server process
        //-----------------------------------------------------------   
        }else if(FD_ISSET(current_process.fd[READ], &readfds)) {

            // get header byte
            // 0x00 -> normal chat message: foward to client
            // 0x01 -> some client joined
            // 0x02 -> some client left
            char header; 

            n = read(current_process.fd[READ], &header , sizeof(header));
            check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);

            //*********************************************************************
            // A new message has been recived. Forward message to pocess's client
            //*********************************************************************
            if(header == (char) MESSAGE_BYTE){ 
                //uint16_t nameLengthNBO;
                uint8_t nameLength;
                uint16_t messageLengthNBO;
                uint16_t messageLength;
                uint16_t byteCount = 0;
                char messageBuff[256];

                //sent header byte
                n = write(sock, &MESSAGE_BYTE , sizeof MESSAGE_BYTE);
                check_error_write(n,"Error writing to pipe",sock);

                //read length of Name
                n = read(current_process.fd[READ], &nameLength, sizeof nameLength);
                check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);

                //send length of Name
                n = write(sock, &nameLength , sizeof nameLength);
                check_error_write(n,"Error writing to pipe",sock);

                //read the name
                bzero(buffer,256);
                tmpNameByteCount = 0;

                // read untill the entire message has arrived
                while(tmpNameByteCount < nameLength){
                    bzero(messageBuff,256);
                    n = read(current_process.fd[READ], messageBuff , (int) (nameLength - tmpNameByteCount));
                    check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);
                    strncat(buffer, messageBuff, strlen(messageBuff));
                    tmpNameByteCount += n; // increment byte count
                }

                // have name now send
                n = write(sock, buffer , (int) nameLength);
                check_error_write(n,"Error writing to pipe",sock);

                //read length of message
                n = read(current_process.fd[READ], &messageLengthNBO, sizeof messageLengthNBO);
                check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);

                // send the length of the message
                n = write(sock, &messageLengthNBO , sizeof messageLengthNBO);
                check_error_write(n,"Error writing to pipe",sock);
                messageLength = ntohs(messageLengthNBO);

                //read the message
                bzero(buffer,256);
                byteCount = 0;

                // read untill the entire message has arrived
                while(byteCount < messageLength){
                    bzero(messageBuff,256);
                    n = read(current_process.fd[READ], messageBuff , messageLength - byteCount);
                    check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);
                    strncat(buffer, messageBuff, strlen(messageBuff));
                    byteCount += n; // increment byte count
                }

                // have message now send
                n = write(sock, buffer , messageLength);
                check_error_write(n,"Error writing to pipe",sock);

                //*********************************************************************
                // A new client has joined. Forward message to pocess's client
                //*********************************************************************
            } else if(header == (char) JOIN_BYTE){ 
                //uint16_t nameLengthNBO;
                uint8_t nameLength;
                char nameBuff[256];

                //sent header byte
                n = write(sock, &JOIN_BYTE , sizeof JOIN_BYTE);
                check_error_write(n,"Error writing to pipe",sock);

                //read length of Name
                n = read(current_process.fd[READ], &nameLength, sizeof nameLength);
                check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);

                //send length of Name
                n = write(sock, &nameLength , sizeof nameLength);
                check_error_write(n,"Error writing to pipe",sock);
                //nameLength = ntohs(nameLengthNBO);

                //read the name
                bzero(buffer,256);
                tmpNameByteCount = 0;

                while(tmpNameByteCount < nameLength){
                    bzero(nameBuff,256);
                    n = read(current_process.fd[READ], nameBuff , (int) (nameLength - tmpNameByteCount));
                    check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);
                    strncat(buffer, nameBuff, strlen(nameBuff));
                    tmpNameByteCount += n; // increment byte count
                }

                // have name now send
                n = write(sock, buffer , (int) nameLength);
                check_error_write(n,"Error writing to pipe",sock);

            //*********************************************************************
            // An existing client has left. Forward message to proces's client
            //*********************************************************************
            }else if(header == (char) LEAVE_BYTE){
                //uint16_t nameLengthNBO;
                uint8_t nameLength;
                //uint16_t byteCount = 0;
                char nameBuff[256];

                //sent header byte
                n = write(sock, &LEAVE_BYTE , sizeof LEAVE_BYTE);
                check_error_write(n,"Error writing to pipe",sock);

                //read length of Name
                n = read(current_process.fd[READ], &nameLength, sizeof nameLength);
                check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);

                //send length of Name
                n = write(sock, &nameLength , sizeof nameLength);
                check_error_write(n,"Error writing to pipe",sock);

                //read the name
                bzero(buffer,256);
                tmpNameByteCount = 0;

                while(tmpNameByteCount < nameLength){
                    bzero(nameBuff,256);
                    n = read(current_process.fd[READ], nameBuff , (int) (nameLength - tmpNameByteCount));
                    check_error_read(n, "ERROR reading from socket",current_process.fd[READ]);
                    strncat(buffer, nameBuff, strlen(nameBuff));
                    tmpNameByteCount += n; // increment byte count
                }

                // have name now send
                n = write(sock, buffer , nameLength);
                check_error_write(n,"Error writing to pipe",sock);

            }
        //-----------------------------------------------------------
        // connection has timed out teminate connection
        //-----------------------------------------------------------   
        }else if(selectReturn == 0){
            // thirty seconds has elapsed with no activity
            // end the connection
            char *line = concat(current_process.name, " No activity for 30 seconds!\n");
            write_log(line);

            close(sock);
            return;
        }  
    }

}

int main(int argc, char *argv[])
{

     daemonize();

     // set signal handelers
     signal(SIGCHLD, sigchld_handler);
     signal(SIGTERM, sigterm_handler);

     // Map a shared array of process information (To be shared by all server processes)
     head = mmap(NULL, sizeof (struct node) * MAX_NUM_CLIENT, PROT_READ | PROT_WRITE, 
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

     // Map a shared array of log file lines (To be shared by all server processes)
     LOG_CONTENT = mmap(NULL, sizeof (struct LOG_LINE) * MAX_LOG, PROT_READ | PROT_WRITE, 
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // intitialize array of server process info
    int i;
     for (i = 0; i < MAX_NUM_CLIENT; i++) {
         strcpy(head[i].name , "no_name");
         head[i].pid = -1;
         pipe(head[i].fd);
     }

     // Initialize log file content array
     for (i = 0; i < MAX_LOG; i++) {
         strcpy(LOG_CONTENT[i].line, "nothing");
     }

     int sockfd, newsockfd, portno, pid;
     socklen_t clilen;
     struct sockaddr_in serv_addr, cli_addr;

     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }

     char pidstr[12];
     int daemon_pid = getpid();
     snprintf(pidstr, 12, "%d", daemon_pid);
     printf("daemon pid %s",pidstr);
     char * file_path = concat("/var/tmp", "/");
     char * file_name = concat("server379",pidstr);
     char * full_path_file_name = concat(file_path, file_name);
     char * log_filename = concat(full_path_file_name, ".log");

     fp = fopen(log_filename, "w+");

     // Socket establishment
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     check_error(sockfd, "ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr ;
     serv_addr.sin_port = htons(portno);
     check_error(bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)),  "ERROR opening socket");
     listen(sockfd,MAX_NUM_CLIENT); 

     clilen = sizeof(cli_addr);

	
     // Listen for new connections
     while (1) {
         newsockfd = accept(sockfd, 
               (struct sockaddr *) &cli_addr, &clilen);
         check_error(newsockfd, "ERROR on accept");

         pid = fork();
         check_error(pid, "ERROR on fork");
         if (pid == 0)  {
             close(sockfd);
             handshake_protocol(newsockfd); // initiate connnection with the client
             exit(2);
         }
         else {
             close(newsockfd);
         }
     } 
     close(sockfd);
     return 0; /* we never get here */
}

