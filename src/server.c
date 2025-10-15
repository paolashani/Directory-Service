#include <stdio.h> //Standard I/O functions
#include <stdlib.h> //Standard library functions
#include <string.h> //String manipulation functions
#include <unistd.h> //POSIX API (close, read, write)
#include <pthread.h> //POSIX threads
#include <netinet/in.h> //Internet address structures
#include <arpa/inet.h> //Functions for IP address manipulation
#include <ctype.h> //Character checks

#define PORT 8080 //Server port number
#define MAX_CLIENTS 10 //Maximum concurrent clients
#define BUFFER_SIZE 1024 //Buffer size for incoming messages
#define MAX_VARIABLES 100 //Maximum number of stored variables

typedef enum { INT, FLOAT, STRING, ARRAY } VarType; //Enum for the supported variable types

typedef struct { //Struct representing a variable with its name, type and value
    char name[64]; //Name of the variable
    VarType type; //Type of the variable
    union { //Holds only one of the possible value types
        int i_val; //Integer value
        float f_val; //Float value
        char s_val[256]; //String value
        float arr_val[100]; //Array of floats
    };
    int arr_len; //Length of array (if type is ARRAY)
} Variable; //Defines the structure named 'Variable'

Variable vars[MAX_VARIABLES]; //Array of stored variables
int var_count = 0; //Current number of variables
pthread_mutex_t var_mutex = PTHREAD_MUTEX_INITIALIZER; //Mutex to protect variable access

void list_vars(int client_sock) { //Sends the list of variable names to the client
    char buffer[BUFFER_SIZE]; //Temporary buffer
    buffer[0] = '\0'; //Initialize buffer as an empty C-string before concatenation
    pthread_mutex_lock(&var_mutex); //Lock the mutex to ensure exclusive access to shared variables
    for (int i = 0; i < var_count; i++) { //Loop through all currently stored variables
        strcat(buffer, vars[i].name); //Append the current variable name to the output buffer
        strcat(buffer, "\n"); //Add a newline character after the variable name
    }
    pthread_mutex_unlock(&var_mutex); //Release the mutex to allow other threads to access shared data
    send(client_sock, buffer, strlen(buffer), 0); //Send the buffer content to the connected client
}

void read_var(int client_sock, char* name) { //Function to send the value of a variable to a client based on its name
    char buffer[BUFFER_SIZE]; //Create a buffer to store the formatted output
    buffer[0] = '\0'; //Initialize buffer as an empty C-string
    pthread_mutex_lock(&var_mutex); //Lock the mutex to safely access the shared variable list
    for (int i = 0; i < var_count; i++) { //Iterate through all stored variables
        if (strcmp(vars[i].name, name) == 0) { //Check if the current variable's name matches the requested name
            switch (vars[i].type) { //Determine how to print the value based on its type
                case INT: //If the variable is an integer
                    sprintf(buffer, "%d\n", vars[i].i_val); //Format the integer into the buffer
                    break;
                case FLOAT: //If the variable is a float
                    sprintf(buffer, "%.2f\n", vars[i].f_val); //Format the float with 2 decimal digits
                    break;
                case STRING: //If the variable is a string
                    sprintf(buffer, "%s\n", vars[i].s_val); //Copy the string value into the buffer
                    break;
                case ARRAY: //If the variable is an array of floats
                    for (int j = 0; j < vars[i].arr_len; j++) { //Loop through all elements of the array
                        char temp[32]; //Temporary string to hold formatted number
                        sprintf(temp, "%.2f ", vars[i].arr_val[j]); //Format each float with 2 decimals
                        strcat(buffer, temp); //Append it to the output buffer
                    }
                    strcat(buffer, "\n"); //Add newline after the array output
                    break;
            }
            break; //Exit the loop once the variable is found and processed
        }
    }
    if (strlen(buffer) == 0) //If no variable was found and the buffer is still empty
        sprintf(buffer, "Variable not found.\n"); //Write an error message into the buffer
    pthread_mutex_unlock(&var_mutex); //Unlock the mutex to allow other threads to access shared data
    send(client_sock, buffer, strlen(buffer), 0); //Send the response (either value or error) to the client
}

void set_var(char* command) { //Sets or overrides a variable with a new value sent by the client
    pthread_mutex_lock(&var_mutex); //Lock the mutex to ensure exclusive access to the shared variable list

    char* name = strtok(command, "="); //Extract variable name
    char* value = strtok(NULL, "="); //Extract value
    if (!name || !value) { //Check if either the variable name or value is missing after parsing
        pthread_mutex_unlock(&var_mutex); //Unlock the mutex before exiting to avoid deadlock
        return; //Exit the function early since the input is invalid
    }

    while (*name == ' ') name++; //Trim whitespace
    while (*value == ' ') value++; //Trim whitespace

    Variable* var = NULL; //Declare a pointer to Variable struct, to be used for storing or updating a variable
    int is_array = strchr(value, '{') != NULL; //Check if the value contains a '{', indicating it's an array assignment

    if (is_array) { //If the value is an array (detected by the presence of '{')
        char array_name[64]; //Temporary buffer to store the array variable's name
        char* b = strchr(name, '['); //Look for the '[' to separate name from index
        if (b) *b = '\0'; //If found, terminate the name string before the '[' to get the base variable name
        strncpy(array_name, name, sizeof(array_name)); //Copy the cleaned variable name into array_name
        array_name[sizeof(array_name)-1] = '\0'; //Ensure null-termination to avoid buffer overflows

        for (int i = 0; i < var_count; i++) { //Search for an existing variable with this name
            if (strcmp(vars[i].name, array_name) == 0) { //
                var = &vars[i]; //If found, assign its pointer to var for updating
                break;
            }
        }

        if (!var) { //If the variable doesn't exist, create a new one
            var = &vars[var_count++]; //Allocate the next slot and advance the count
            strcpy(var->name, array_name); //Store the variable name
        }

        var->type = ARRAY; //Set the type of the variable to ARRAY

        char* open_brace = strchr(value, '{'); //Find the opening brace of the array values
        char* close_brace = strchr(value, '}'); //Find the closing brace
        if (!open_brace || !close_brace) { //If either is missing, the format is invalid
            pthread_mutex_unlock(&var_mutex); //Unlock the mutex before exiting
            return; //Abort processing the current command
        }

        *close_brace = '\0'; //Remove closing brace
        open_brace++; //Move past the opening '{' to point to the first value

        var->arr_len = 0; //Initialize the array length counter
        char* token = strtok(open_brace, ","); //Tokenize the input string by comma to extract individual values
        while (token && var->arr_len < 100) { //Loop through each token until no more or array limit reached
            var->arr_val[var->arr_len++] = atof(token); //Convert the token to float and store it in the array
            token = strtok(NULL, ","); //Get the next token in the string
        }

    } else { //Handle scalar variable (INT, FLOAT, STRING)
        for (int i = 0; i < var_count; i++) { //Search for an existing variable with the same name
            if (strcmp(vars[i].name, name) == 0) {
                var = &vars[i]; //If found, reuse it for updating
                break;
            }
        }
        if (!var) { //If the variable does not exist, create a new one
            var = &vars[var_count++]; //Allocate a new slot and increment the count
            strcpy(var->name, name); //Store the variable's name
        }

        if (value[0] == '"' && value[strlen(value)-1] == '"') { //If the value is enclosed in double quotes, treat it as a string
            var->type = STRING; //Set type to STRING
            strncpy(var->s_val, value + 1, strlen(value) - 2); //Copy the string without quotes
            var->s_val[strlen(value) - 2] = '\0'; //Null-terminate the resulting string
        } else if (strchr(value, '.')) { //If the value contains a dot, treat it as a float
            var->type = FLOAT; //Set type to FLOAT
            var->f_val = atof(value); //Convert string to float and store it
        } else { //Otherwise, treat the value as an integer
            var->type = INT; //Set type to INT
            var->i_val = atoi(value); //Convert string to integer and store it
        }
    }

    pthread_mutex_unlock(&var_mutex); //Unlock the mutex
}

void* handle_client(void* arg) { //Thread function to handle a client connection
    int client_sock = *(int*)arg; //Extract the client socket descriptor from the passed pointer
    char buffer[BUFFER_SIZE]; //Buffer to store incoming data from the client
    send(client_sock, "directory-service> ", 20, 0); //Send initial prompt to the client

    while (1) { //Infinite loop to continuously handle commands from the client
        memset(buffer, 0, BUFFER_SIZE); //Clear the buffer to remove any leftover data
        int read_size = recv(client_sock, buffer, BUFFER_SIZE, 0); //Receive data from the client socket
        if (read_size <= 0) break; //If the client disconnected or error occurred, exit the loop
        buffer[strcspn(buffer, "\r\n")] = 0; //Remove newline from the input

        //Dispatch based on client command
        if (strncmp(buffer, "list-vars", 9) == 0) { //If client typed "list-vars"
            list_vars(client_sock); //Send the list of variable names to the client
        } else if (strncmp(buffer, "read ", 5) == 0) { //If client typed "read <varname>"
            read_var(client_sock, buffer + 5); //Read and return the value of the specified variable
        } else if (strncmp(buffer, "set ", 4) == 0) { //If client typed "set <var=value>"
            set_var(buffer + 4); //Set or update the specified variable
        } else if (strncmp(buffer, "exit", 4) == 0) { //If client typed "exit"
            break; //Exit the loop and close the connection
        } else { //Unknown command
            char* msg = "Invalid command\n"; //Prepare an error message
            send(client_sock, msg, strlen(msg), 0); //Send the error message to the client
        }
        send(client_sock, "directory-service> ", 20, 0); //Send the prompt again to indicate readiness for the next command
    }

    close(client_sock); //Close the client socket
    free(arg); //Free memory allocated for client socket
    pthread_exit(NULL); //Exit the thread
}

int main() { //Main server function
    int server_fd, new_socket; //server_fd is the socket file descriptor for the server: new_socket will hold each client's connection socket
    struct sockaddr_in address; //Structure that holds the server's IP address and port information
    socklen_t addrlen = sizeof(address); //Length of the address structure, used in bind() and accept()

    server_fd = socket(AF_INET, SOCK_STREAM, 0); //Create a TCP socket
    address.sin_family = AF_INET; //IPv4
    address.sin_addr.s_addr = INADDR_ANY; //Accept any incoming connection
    address.sin_port = htons(PORT); //Set server port

    bind(server_fd, (struct sockaddr*)&address, sizeof(address)); //Bind socket to port
    listen(server_fd, MAX_CLIENTS); //Start listening for clients

    printf("Server listening on port %d...\n", PORT); //Display a message indicating that the server is now running and listening for client connections on the specified port

    while (1) { //Accept clients in an infinite loop
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen); //Accept new client
        int* pclient = malloc(sizeof(int)); //Allocate memory for client socket
        *pclient = new_socket; //Store the accepted client socket descriptor into the allocated memory
        pthread_t tid; //Create a new thread for the client
        pthread_create(&tid, NULL, handle_client, pclient); //Create a new thread to handle the client, passing the client socket as argument
        pthread_detach(tid); //Detach thread (no need to join)
    }

    return 0; //Exit the program
}
