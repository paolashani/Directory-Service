#include <stdio.h> //For standard I/O functions like printf, fgets
#include <stdlib.h> //For standard library functions
#include <string.h> //For string manipulation functions
#include <unistd.h> //For close(), read(), write(), etc.
#include <arpa/inet.h> //For inet_pton(), htons(), socket structures
#include <sys/select.h> //For select(), FD_SET macros (I/O multiplexing)

#define PORT 8080 //The port number the client will connect to
#define BUFFER_SIZE 1024 //Size of the input/output buffers

int main() {
    int sock; //File descriptor for the socket
    struct sockaddr_in serv_addr; //Structure that holds server's address info
    char buffer[BUFFER_SIZE]; //Buffer for receiving data from the server
    char input[BUFFER_SIZE]; //Buffer for reading user input from terminal

    sock = socket(AF_INET, SOCK_STREAM, 0); //Create a TCP socket

    serv_addr.sin_family = AF_INET; //Set address family to IPv4
    serv_addr.sin_port = htons(PORT); //Set port number (converted to network byte order)
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr); //Convert IP address string to binary form

    connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); //Connect to the server

    fd_set readfds; //Declare a file descriptor set for use with select()
    while (1) { //Infinite loop to keep communicating with the server

        FD_ZERO(&readfds); //Clear all entries from the set
        FD_SET(sock, &readfds); //Add the server socket to the set
        FD_SET(STDIN_FILENO, &readfds); //Add standard input (keyboard) to the set

        select(sock + 1, &readfds, NULL, NULL, NULL); //Wait for input from either socket or keyboard

        if (FD_ISSET(sock, &readfds)) { //If there is data to read from the server
            memset(buffer, 0, BUFFER_SIZE); //Clear the buffer
            int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0); //Receive message from server
            if (bytes <= 0) break; //If connection closed or error, exit loop
            printf("%s", buffer); //Print server response
            fflush(stdout); //Flush output buffer to ensure prompt is shown
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) { //If there is user input
            fgets(input, BUFFER_SIZE, stdin); //Read user input from terminal
            send(sock, input, strlen(input), 0); //Send user input to the server
            if (strncmp(input, "exit", 4) == 0) //If user typed "exit"
                break; //Exit the loop and close the socket
        }
    }

    close(sock); //Close the socket
    return 0; //Exit the program
}
