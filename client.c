#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <signal.h>

#define MAX_NUM_CLIENT 100

const char FIRST_BYTE = 0xCF; 
const char SECOND_BYTE = 0xA7; 
const char MESSAGE_BYTE = 0x00;
const char JOIN_BYTE = 0x01;
const char LEAVE_BYTE = 0x02;

char clientNames[MAX_NUM_CLIENT][256];

// remove a client from the local list
void removeClient(char * clinetName){
    int i;
    for(i = 0;i < MAX_NUM_CLIENT; i++){
        if (strcmp(clientNames[i], clinetName) == 0){
            strcpy(clientNames[i], "no_name"); 
            return;
        }
    }
}

// add a client to the local list
void addClient(char * clinetName){
    int i;
    for(i = 0;i < MAX_NUM_CLIENT; i++){
        if (strcmp(clientNames[i], "no_name") == 0){
            strcpy(clientNames[i], clinetName); 
            return;
        }
    }
}

void printConnectedClients(){
    int i;
    printf("Connected clients :\n");
    for(i = 0;i < MAX_NUM_CLIENT; i++){
        if (strcmp(clientNames[i], "no_name") != 0){
            printf("%s\n",clientNames[i]); 
        }
    }
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
            printf("Connection ended. Socket %d hung up.\n", fd);
        } else {
            perror(msg);
        }
        close(fd); // bye!
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    struct timeval tv;
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int selectReturn;
    uint16_t zeroLength = 0;
    uint16_t zeroLengthNBO = htons(zeroLength);
    tv.tv_sec = 20; // set to 20 b/c delays and stuff
    tv.tv_usec = 0;

    int j;
    for (j = 0; j < MAX_NUM_CLIENT; j++) {
        strcpy(clientNames[j], "no_name");
    }

    char buffer[256];
    if (argc < 3) {
       printf("usage %s hostname port\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    check_error(sockfd, "ERROR on sock");
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        printf("ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    check_error(connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)), "ERROR connecting");

    // recive the header
    char header;
 
    bzero(buffer,256);
    n = read(sockfd, &header, sizeof(header));
    check_error_read(n, "ERROR reading from socket",sockfd);

    if(header == (char) FIRST_BYTE){
        printf("header 1: %x\n", header & 0xff);
    }else{
        //Terminate
        printf("header 1: %x\n", header & 0xff);
        exit(0);
    }

    n = read(sockfd, &header, sizeof(header));
    check_error_read(n, "ERROR reading from socket",sockfd);

    if(header == (char) SECOND_BYTE){
        printf("header 2: %x\n", header & 0xff);
    }else{
        //terminate
        printf("header 2: %x\n", header & 0xff);
        exit(0);
    }

    uint16_t num_connected_clientNBO = 0;
    // reading the number of clients
    n = read(sockfd, &num_connected_clientNBO, sizeof(num_connected_clientNBO));
    check_error_read(n, "ERROR reading from socket",sockfd);

    uint16_t num_connected_client = ntohs(num_connected_clientNBO);
    printf("There are %d clients connected.\n", num_connected_client);
    printf("Connected clients:\n");

    //read all the other clinent names
    int i;
    for(i = 0; i< num_connected_client; i++){
        //get length of name
        uint8_t lengthOfClientName ;
        uint8_t byteCount = 0;
        char nameBuff[256];
        n = recv(sockfd, &lengthOfClientName, sizeof(lengthOfClientName),0);
        check_error_read(n, "ERROR reading from socket",sockfd);
        bzero(buffer, 256);

        while(byteCount < lengthOfClientName){
            bzero(nameBuff,256);
            n = recv(sockfd, nameBuff , lengthOfClientName - byteCount, 0);
            check_error_read(n, "ERROR reading from socket",sockfd);
            strncat(buffer, nameBuff, strlen(nameBuff));
            byteCount += n; // increment byte count
        }

        int k;
        // Insert current client names to local list
        for (k = 0; k < MAX_NUM_CLIENT; k++) {
            if (strcmp(clientNames[k], "no_name") == 0){
                strcpy(clientNames[k], buffer) ;
                break; 
            }
        }

        printf("%s\n" , buffer);
        bzero(buffer, 256);
    }


    char *client_name = argv[3];
    bzero(buffer, 256);

    strncat(buffer,client_name,strlen(client_name));


    // send client name length
    uint8_t clientNameLength = strlen(buffer);
    n = write(sockfd, &clientNameLength, sizeof(clientNameLength));
    check_error_write(n, "ERROR writing to socket",sockfd);

    // send client name
    n = send(sockfd, buffer, clientNameLength,0);
    check_error_write(n, "ERROR writing name tp soclet socket",sockfd);

    bzero(buffer, 256);
    printf("%s\n", buffer);

    // Enter chat mode
    fd_set connset, readfds;
    FD_ZERO(&connset);

    FD_SET(sockfd, &connset); /* add sockfd to connset */
    FD_SET(STDIN_FILENO, &connset); /* add STDIN to connset */

    int shouldPrintClientPrompt = 1; // detrmine if anything should be printed to the screen

    while (1) {

        readfds = connset;
	    tv.tv_sec = 20; // set to 20 b/c delays and stuff
	    tv.tv_usec = 0;
        // prompt user for input
        if(shouldPrintClientPrompt){
            printf("List clients with '*' or type message: ");
        }

        fflush(stdout);
        if((selectReturn = select(FD_SETSIZE, &readfds, NULL, NULL, &tv)) < 0) {
            fprintf(stdout, "select() error\n");
            exit(0);
        }       

        shouldPrintClientPrompt = 1;

	//***************************************************************
	// Recived data from the server
	//***************************************************************
        if (FD_ISSET(sockfd, &readfds)) {

            //printf("In socket descriptor\n");
            bzero(buffer, 256);

            // get header byte
            char header; 
            // get header byte
            // 0x00 -> normal chat message
            // 0x01 -> some client joined
            // 0x02 -> some client left

            n = recv(sockfd, &header , sizeof(header),0);
            check_error_read(n, "ERROR receiving header: ",sockfd);
            //printf("header: %x\n", header & 0xff);

            // -------------------------------------------------------------
            // A message has been recived
            // -------------------------------------------------------------
            if(header == (char) MESSAGE_BYTE){
                //printf("reading the message\n");
                uint8_t lengthOfName;
                uint16_t lengthOfMessageNBO;
                uint16_t lengthOfMessage;
                uint16_t byteCount = 0;
                uint8_t nameByteCount = 0;
                char messageBuff[256];
                char senderName[256];
                // read length of name
                n = recv(sockfd, &lengthOfName, sizeof(lengthOfName),0);
                check_error_read(n, "ERROR reading from socket",sockfd);
            
                //read the name
                bzero(buffer, 256);
                nameByteCount = 0; // number of bytes recived
                

                while(nameByteCount < lengthOfName){
                    bzero(messageBuff,256);
                    n = recv(sockfd, messageBuff , lengthOfName - nameByteCount, 0);
                    check_error_read(n, "ERROR reading from socket",sockfd);

                    strncat(buffer, messageBuff, strlen(messageBuff));
                    nameByteCount += n; // increment byte count
                }

                bzero(senderName,256);
                strncat(senderName ,buffer,strlen(buffer));

                //read the length of the message
                n = read(sockfd, &lengthOfMessageNBO, sizeof(lengthOfMessageNBO));
                check_error_read(n, "ERROR reading from socket",sockfd);

                lengthOfMessage = ntohs(lengthOfMessageNBO);

                bzero(buffer, 256);
                byteCount = 0; // number of bytes recived

                while(byteCount < lengthOfMessage){
                    bzero(messageBuff,256);
                    n = recv(sockfd, messageBuff , lengthOfMessage - byteCount, 0);
                    check_error_read(n, "ERROR reading from socket",sockfd);

                    strncat(buffer, messageBuff, strlen(messageBuff));
                    byteCount += n; // increment byte count
                }

                //print the recived message
                printf("message from '%s': %s \n", senderName, buffer);

            // -------------------------------------------------------------
            // A new client has joined the chat
            // -------------------------------------------------------------
            }else if(header == (char) JOIN_BYTE){ 
                uint8_t lengthOfName;
                uint16_t byteCount;
                char nameBuff[256];

                n = recv(sockfd, &lengthOfName, sizeof(lengthOfName),0);
                check_error_read(n, "ERROR reading from socket",sockfd);
              
                //read the name
                bzero(buffer, 256);
                byteCount = 0; // number of bytes recived

                while(byteCount < lengthOfName){
                    bzero(nameBuff,256);
                    n = recv(sockfd, nameBuff , lengthOfName - byteCount, 0);
                    check_error_read(n, "ERROR reading from socket",sockfd);

                    strncat(buffer, nameBuff, strlen(nameBuff));
                    byteCount += n; // increment byte count
                }

		//Inform the user
                printf("Client '%s' has entered the chat.\n", buffer);
                // add the new client to the list
                addClient(buffer);
                bzero(buffer, 256);

            // -------------------------------------------------------------
            // A client has left the chat
            // -------------------------------------------------------------
            }else if(header == (char) LEAVE_BYTE){
               // printf("client has left \n");

                uint8_t lengthOfName;
                uint8_t byteCount;
                char nameBuff[256];

                n = recv(sockfd, &lengthOfName, sizeof(lengthOfName),0);
                check_error_read(n, "ERROR reading from socket",sockfd);
   
                //read the name
                bzero(buffer, 256);
                byteCount = 0; // number of bytes recived

                while(byteCount < lengthOfName){
                    bzero(nameBuff,256);
                    n = recv(sockfd, nameBuff , lengthOfName - byteCount, 0);
                    check_error_read(n, "ERROR reading from socket",sockfd);

                    strncat(buffer, nameBuff, strlen(nameBuff));
                    byteCount += n; // increment byte count
                }
		
		// inform the user
                printf("Client '%s' has left the chat.\n", buffer);
                // remove client from the list
                removeClient(buffer);
                bzero(buffer, 256);

            }else{
                // message of unknown format recived
                printf("Unknown format message recived\n");
                //terminate programm
                exit(0);

            }
	//***************************************************************
	// Recived command line input STDIN
	//***************************************************************
        }else if (FD_ISSET(STDIN_FILENO, &readfds)) {
            /* data at STDIN */
            bzero(buffer, 256);

            n = read(STDIN_FILENO,buffer, 256);
            check_error_read(n, "ERROR reading from socket",STDIN_FILENO);

            if(strlen(buffer) == 2 &&  strncmp(buffer,"*",1) == 0){ // list users
                printConnectedClients();
            }else{ // send the message
                uint16_t messageLength = strlen(buffer) - 1;
                uint16_t messageLengthNBO = htons(messageLength);
                // send to server
                // send size of message            
                n = write(sockfd, &messageLengthNBO, sizeof(messageLengthNBO));
                check_error_write(n, "ERROR send buffer",sockfd);
                //send actual message
                n = write(sockfd, buffer , messageLength);
                check_error_write(n, "ERROR send buffer",sockfd);
            }

        }else if(selectReturn == 0){
            // A time out has occured send the keep alive message
            n = send(sockfd, &zeroLengthNBO, sizeof(zeroLengthNBO),0);
            check_error_write(n, "ERROR send buffer",sockfd);
            shouldPrintClientPrompt = 0;
        }    
    }
      
    close(sockfd);
    return 0;
}
