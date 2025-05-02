#include "download.h"

int parse(char *input, struct URL *url) {

    regex_t regex;
    regcomp(&regex, BAR, 0);
    if (regexec(&regex, input, 0, NULL, 0)) return -1;

    regcomp(&regex, AT, 0);
    if (regexec(&regex, input, 0, NULL, 0) != 0) { //This case is for when there are no user credentials in the URL
        
        sscanf(input, HOST_REGEX, url->host);
        strcpy(url->user, DEFAULT_USER);
        strcpy(url->password, DEFAULT_PASSWORD);

    } else { // This case is for when there are user credentials in the URL (username:password@)

        sscanf(input, HOST_AT_REGEX, url->host);
        sscanf(input, USER_REGEX, url->user);
        sscanf(input, PASS_REGEX, url->password);
    }

    sscanf(input, RESOURCE_REGEX, url->resource);
    strcpy(url->file, strrchr(input, '/') + 1);

    struct hostent *h;
    if (strlen(url->host) == 0) return -1;
    if ((h = gethostbyname(url->host)) == NULL) {
        printf("Invalid hostname '%s'\n", url->host);
        exit(-1);
    }
    strcpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr_list[0])));

    return !(strlen(url->host) && strlen(url->user) && 
           strlen(url->password) && strlen(url->resource) && strlen(url->file));
}

int createSocket(char *ip, int port) {

    int sockfd;
    struct sockaddr_in server_addr;

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);  
    server_addr.sin_port = htons(port); 
    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }
    
    return sockfd;
}

int authConn(const int socket, const char* user, const char* pass) {
    size_t userCommandLength = 5+strlen(user)+2;
    size_t passCommandLength = 5+strlen(pass)+2;
    char* userCommand = (char *)malloc(userCommandLength); sprintf(userCommand, "user %s\r\n", user);
    char* passCommand = (char *)malloc(passCommandLength); sprintf(passCommand, "pass %s\r\n", pass);
    char answer[MAX_LENGTH];
    
    write(socket, userCommand, userCommandLength);
    if (readResponse(socket, answer) != SV_READY4PASS) {
        printf("Unknown user '%s'. Abort.\n", user);
        free(userCommand);
        free(passCommand);
        exit(-1);
    }else{
        printf("Username successful\n");
    }

    if(write(socket, passCommand, passCommandLength) < 0){
        printf("Unknown password '%s'. Abort.\n", pass);
        free(userCommand);
        free(passCommand);
        exit(-1);
    }else{
        printf("Password successful\n");
    }
    return readResponse(socket, answer);
}

int passiveMode(const int socket, char *ip, int *port) {

    char answer[MAX_LENGTH];
    int ip1, ip2, ip3, ip4, port1, port2;
    write(socket, "pasv\r\n", 6);
    if (readResponse(socket, answer) != SV_PASSIVE) return -1;

    sscanf(answer, PASSIVE_REGEX, &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

    return SV_PASSIVE;
}

int readResponse(const int socket, char* buffer) {
    char byte, exception = 0;
    int index = 0, responseCode;
    ResponseState state = START;
    memset(buffer, 0, MAX_LENGTH);

    while (state != END) {
        if (read(socket, &byte, 1) <= 0) {
            perror("read");
            return -1;
        }
        printf("%c", byte);

        switch (state) {
            case START:
                if (byte == '-') state = MULTIPLE;
                else if (byte == ' ' && exception) {
                    exception = 0;
                    state = EXCEPTION;
                }else if (byte == ' ') {
                    state = SINGLE;
                    exception = 0;
                }else if (byte == '\n') {
                    exception = 0;
                    state = END;
                }else {
                    buffer[index++] = byte;
                    exception = 0;
                }
                break;
            case SINGLE:
                if (byte == '\n') state = END;
                else buffer[index++] = byte;
                break;
            case MULTIPLE:
                if (byte == '\n') {
                    state = START;
                    index = 0;
                    exception = 1;
                } else {
                    buffer[index++] = byte;
                }
                break;
            case END:
                break;
            case EXCEPTION:
                if (byte == '\n') state = START;
                break;
            default:
                break;
        }
    }

    sscanf(buffer, RESPCODE_REGEX, &responseCode);
    return responseCode;
}

int requestResource(const int socket, char *resource) {
    size_t fileCommandLength = 5 + strlen(resource) + 2;
    char* fileCommand = (char *)malloc(fileCommandLength);
    char answer[MAX_LENGTH];
    sprintf(fileCommand, "retr %s\r\n", resource);
    write(socket, fileCommand, fileCommandLength);
    free(fileCommand);
    return readResponse(socket, answer);
}

int getResource(const int socketA, const int socketB, char *filename) {
    FILE *fd = fopen(filename, "wb");
    if (fd == NULL) {
        printf("Error opening or creating file '%s'\n", filename);
        exit(-1);
    }

    char buffer[MAX_LENGTH];
    int bytes_read;
    int total_bytes = 0;

    while ((bytes_read = read(socketB, buffer, MAX_LENGTH)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, fd);
        if (bytes_written != bytes_read) {
            perror("File write error");
            fclose(fd);
            return -1;
        }
        total_bytes += bytes_read;
    }

    if (bytes_read < 0) {
        perror("Socket read error");
        fclose(fd);
        return -1;
    }

    fclose(fd);

    char answer[MAX_LENGTH];
    return readResponse(socketA, answer);
}

int closeConnection(const int socketA, const int socketB) {
    
    char answer[MAX_LENGTH];
    write(socketA, "quit\r\n", 6);
    if(readResponse(socketA, answer) != SV_GOODBYE) {
        printf("closeConncetion error");
        return -1;
    }
    return close(socketA) || close(socketB);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    } 

    struct URL url;
    memset(&url, 0, sizeof(url));
    if (parse(argv[1], &url) != 0) {
        printf("Parse error. Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }
    
    printf("Host: %s\nResource: %s\nFile: %s\nUser: %s\nPassword: %s\nIP Address: %s\n", url.host, url.resource, url.file, url.user, url.password, url.ip);

    char answer[MAX_LENGTH];
    int socketA = createSocket(url.ip, FTP_PORT);
    if (socketA < 0 || readResponse(socketA, answer) != SV_READY4AUTH) {
        printf("Socket to '%s' and port %d failed\n", url.ip, FTP_PORT);
        exit(-1);
    }else{
        printf("Socket to '%s' and port %d succeeded\n", url.ip, FTP_PORT);
    }
    
    if (authConn(socketA, url.user, url.password) != SV_LOGINSUCCESS) {
        printf("Authentication failed with username = '%s' and password = '%s'.\n", url.user, url.password);
        exit(-1);
    }else{
        printf("Authentication succeeded with username = '%s' and password = '%s'.\n", url.user, url.password);
    }
    
    int port;
    char ip[MAX_LENGTH];
    if (passiveMode(socketA, ip, &port) != SV_PASSIVE) {
        printf("Passive mode failed\n");
        exit(-1);
    }else{
        printf("Passive mode succeeded\n");
    }

    int socketB = createSocket(ip, port);
    if (socketB < 0) {
        printf("Socket to '%s:%d' failed\n", ip, port);
        exit(-1);
    }else{
        printf("Socket to '%s:%d' success\n", ip, port);
    }

    int requestresource = requestResource(socketA, url.resource);
    if ((requestresource != SV_CONNECTIONALROPEN) && (requestresource != SV_READY4TRANSFER)) {
        printf("Unknown resource '%s' in '%s:%d'\n", url.resource, ip, port);
        exit(-1);
    }else{
        printf("Transfer ready to begin\n");
    }

    if (getResource(socketA, socketB, url.file) != SV_TRANSFER_COMPLETE) {
        printf("Error transferring file '%s' from '%s:%d'\n", url.file, ip, port);
        exit(-1);
    }

    if (closeConnection(socketA, socketB) != 0) {
        printf("Sockets close error\n");
        exit(-1);
    }

    return 0;
}
