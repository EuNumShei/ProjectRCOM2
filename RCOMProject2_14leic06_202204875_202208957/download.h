#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <termios.h>

#define MAX_LENGTH  500
#define FTP_PORT    21

/* Server responses */
#define SV_READY4AUTH           220
#define SV_READY4PASS           331
#define SV_LOGINSUCCESS         230
#define SV_PASSIVE              227
#define SV_READY4TRANSFER       150
#define SV_CONNECTIONALROPEN    125
#define SV_TRANSFER_COMPLETE    226
#define SV_GOODBYE              221

/* Parser regular expressions */
#define AT              "@"
#define BAR             "/"
#define HOST_REGEX      "%*[^/]//%[^/]"
#define HOST_AT_REGEX   "%*[^/]//%*[^@]@%[^/]"
#define RESOURCE_REGEX  "%*[^/]//%*[^/]/%s"
#define USER_REGEX      "%*[^/]//%[^:/]"
#define PASS_REGEX      "%*[^/]//%*[^:]:%[^@\n$]"
#define RESPCODE_REGEX  "%d"
#define PASSIVE_REGEX   "%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n$)]"

/* Default login for case 'ftp://<host>/<url-path>' */
#define DEFAULT_USER        "anonymous"
#define DEFAULT_PASSWORD    "password"

/* Parser output */
struct URL {
    char host[MAX_LENGTH];      // 'ftp.up.pt'
    char resource[MAX_LENGTH];  // 'parrot/misc/canary/warrant-canary-0.txt'
    char file[MAX_LENGTH];      // 'warrant-canary-0.txt'
    char user[MAX_LENGTH];      // 'username'
    char password[MAX_LENGTH];  // 'password'
    char ip[MAX_LENGTH];        // 193.137.29.15
};

/* Machine states that receives the response from the server */
typedef enum {
    START,
    SINGLE,
    MULTIPLE,
    END,
    EXCEPTION
} ResponseState;

/* 
Analisa um URL, preenche uma estrutura URL com os seus componentes (host, user, password, resource, file, ip) e retorna 0 se não houver erro ou -1 caso contrário
*/
int parse(char *input, struct URL *url);

/* 
Cria um descritor de arquivo de socket baseado no IP e porta do servidor fornecidos. 
Retorna o descritor de arquivo do socket se não houver erro ou -1 caso contrário.
*/
int createSocket(char *ip, int port);

/* 
Autentica a conexão com o servidor usando o descritor de arquivo do socket,
nome de usuário e senha fornecidos.
Retorna o código de resposta do servidor obtido pela operação.
*/
int authConn(const int socket, const char *user, const char *pass);

/* 
Lê a resposta do servidor usando o descritor de arquivo do socket e preenche o buffer com a resposta do servidor.
Retorna o código de resposta do servidor obtido pela operação.
*/
int readResponse(const int socket, char *buffer);

/* 
Entra em modo passivo usando o descritor de arquivo do socket e preenche ip e port com o IP e a porta da conexão de dados.
 Retorna o código de resposta do servidor obtido pela operação.
*/
int passiveMode(const int socket, char* ip, int *port);

/* 
Solicita um recurso ao servidor usando o descritor de arquivo do socket e o recurso desejado.
Retorna o código de resposta do servidor obtido pela operação.
*/
int requestResource(const int socket, char *resource);

/* 
Obtém um recurso do servidor e faz o download no diretório atual usando dois descritores de arquivo de socket e o nome do arquivo desejado.
Retorna o código de resposta do servidor obtido pela operação.
*/
int getResource(const int socketA, const int socketB, char *filename);

/* 
Termina a conexão com o servidor e o próprio socket usando dois descritores de arquivo de socket.
Retorna 0 se não houver erro ao fechar ou -1 caso contrário.
*/
int closeConnection(const int socketA, const int socketB);
