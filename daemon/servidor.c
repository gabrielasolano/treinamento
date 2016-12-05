#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <time.h>

int existe_pagina (char *caminho);
char *recupera_mensagem (char *caminho);
void formato_mesagem();
void inicia_servidor(int *sock, struct sockaddr_in *servidor, int porta);
void responde_cliente (int socket_cliente, char diretorio[]);
void recupera_caminho (char caminho[], char *pagina, char diretorio[]);
void envia_cliente (int sock_cliente, char mensagem[], int size_strlen);
void recupera_arquivo(int sock_cliente, char *caminho);
    
/* Numero maximo de conexoes que o servidor vai esperar */
#define MAXQUEUE 10   
#define BUFFERSIZE BUFSIZ
     
int main (int argc, char **argv)
{
  struct sockaddr_in servidor;
  struct sockaddr_in cliente;
  socklen_t addrlen;
  fd_set read_fds;
  int sock_servidor;
  int sock_cliente;
  int porta;
  int max_fd;
  int i;
  char diretorio[2000];
  
  /* Valida quantidade de parametros de entrada */
  if (argc != 3)
  {
    printf("Quantidade incorreta de parametros.\n");
    formato_mesagem();
    return 1;
  }
  
  /* Recupera e valida a porta */
  porta = atoi(argv[1]);
  if (porta < 0 || porta > 65535)
  {
    printf("Porta invalida!");
    formato_mesagem();
    return 1;
  }
  
  /* Muda o diretorio do processo para o informado na linha de comando */
  if (chdir(argv[2]) == -1)
  {
    perror("chdir()");
    exit(1);
  }
  
  /* Recupera diretorio absoluto */
  getcwd(diretorio, sizeof(diretorio));
  if (diretorio == NULL)
  {
    perror("getcwd()");
    exit(1);
  }
  
  printf("Iniciando servidor na porta: %d e no path: %s\n", porta, diretorio); 
  
  /* socket > configura socket > bind > listen */
  inicia_servidor(&sock_servidor, &servidor, porta);

  /* Configuracao inicial da lista de sockets read_fds */
  FD_ZERO(&read_fds);
  FD_SET(sock_servidor, &read_fds);
  max_fd = sock_servidor;

  /* Loop principal para aceitar e lidar com conexoes do cliente */
  while (1)
  {
    /* Select retorna em read_fds os sockets que estao prontos para leitura */
    if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1)
    {
      perror("select()");
      exit(1);
    }
    
    /* Percorre sockets e verifica se estao na lista read_fds
     * para serem tratados.
     */
    for (i = 0; i <= max_fd; i++)
    {
      /* Existe requisicao de leitura */
      if (FD_ISSET(i, &read_fds))
      {
        /* Socket do servidor: aceita as conexoes novas */
        if (i == sock_servidor)
        {
          addrlen = sizeof(cliente);
          sock_cliente = accept(sock_servidor, (struct sockaddr *)&cliente, 
                                  &addrlen);
          if (sock_cliente == -1)
          {
            perror("accept()");
          }
          else
          {
            FD_SET(sock_cliente, &read_fds);
            if (sock_cliente > max_fd)
              max_fd = sock_cliente;
            printf("Nova conexao de %s no socket %d\n", 
                    inet_ntoa(cliente.sin_addr), (sock_cliente));
          }
        }
        
        /* Socket dos clientes: lida com os dados dos cliente */
        else
        {
          responde_cliente(sock_cliente, diretorio);
          FD_CLR(sock_cliente, &read_fds);
        }
      }
    }
  }
  close(sock_servidor);
  close(sock_cliente);
  return 0;
}


void inicia_servidor(int *sock, struct sockaddr_in *servidor, const int porta)
{
  /* Cria socket para o servidor */
  (*sock) = socket(PF_INET, SOCK_STREAM, 0); 
  if ((*sock) == -1)
  {
    perror("socket()");
    exit(1);
  }
  
  /* Permite reuso da porta : evita 'Address already in use' */
  int yes = 1;
  if (setsockopt((*sock), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
  {
    perror("setsockopt()");
    exit(1);
  }
  
  /* Configura o socket: INADDR_ANY (bind recupera IP maquina que roda o 
   * processo
   */
  (*servidor).sin_family = AF_INET;
  (*servidor).sin_port = htons(porta);
  (*servidor).sin_addr.s_addr = htonl(INADDR_ANY); 
  memset(&((*servidor).sin_zero), '\0', 8); /* Zera o restante da struct */
  
  /* Conecta */
  if (bind((*sock), (struct sockaddr *) &(*servidor), sizeof(struct sockaddr)) 
        == -1)
  {
    perror("bind()");
    exit(1);
  }
  
  /* Espera conexoes : maximo de MAXQUEUE */
  if (listen((*sock), MAXQUEUE) == -1)
  {
    perror("listen()");
    exit(1);
  }
}

 /* Recebe e responde os requests dos clientes */
void responde_cliente (int sock_cliente, char diretorio[])
{
  int bytes;
  int size_strlen;
  char buffer[BUFFERSIZE+1];
  char *http_metodo;
  char *http_versao;
  char *pagina;
  char *contexto;
  char mensagem[BUFFERSIZE+1];
  char caminho[BUFFERSIZE+1];
  
  sleep(5);
  if ((bytes = recv(sock_cliente, buffer, sizeof(buffer), 0)) > 0)
  {
    printf("Recebeu mensagem: %s\n", buffer);
    
    /* Verifica se tem o GET no inicio */
    http_metodo = strtok_r(buffer, " ", &contexto);
    
    /* Se houve o GET no inicio :  */
    if (strncmp(http_metodo, "GET", 3) == 0)
    {
      /* Recupera a versao do protocolo HTTP : HTTP/1.0 ou HTTP/1.1 */
      pagina = strtok_r(NULL, " ", &contexto);
      http_versao = strtok_r(NULL, "\r", &contexto);
      
      /* Se nao houve a versao do protocolo HTTP : request invalido */
      if ((strncmp(http_versao, "HTTP/1.0", 8) != 0) 
            && (strncmp(http_versao, "HTTP/1.1", 8) != 0))
      {
        size_strlen = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");        
        strncpy(mensagem, "HTTP/1.0 400 Bad Request\r\n\r\n", size_strlen);
        envia_cliente(sock_cliente, mensagem, size_strlen);
      }
      
      /* Casos de request valido: GET /path/to/file HTTP/1.0 (ou HTTP/1.1) */
      else
      {
        /* Nao permite acesso fora do diretorio especificado
         * Exemplo : localhost/../../../dado 
         */
        if ((pagina[0] != '/') || (strncmp(pagina, "/..", 3) == 0))
        {
          size_strlen = strlen("HTTP/1.0 403 Forbidden\r\n\r\n");        
          strncpy(mensagem, "HTTP/1.0 403 Forbidden\r\n\r\n", size_strlen);
          envia_cliente(sock_cliente, mensagem, size_strlen);
        }
        else
        {
          memset(caminho, '\0', sizeof(caminho));
          recupera_caminho(caminho, pagina, diretorio);
          if (existe_pagina(caminho))
          {
            recupera_arquivo(sock_cliente, caminho);
          }
          else
          {
            size_strlen = strlen("HTTP/1.0 404 Not Found\r\n\r\n");        
            strncpy(mensagem, "HTTP/1.0 404 Not Found\r\n\r\n", size_strlen);
            envia_cliente(sock_cliente, mensagem, size_strlen);
          }
          memset(caminho, '\0', sizeof(caminho));
        }
      }
    }
    
    /* Se nao houver o GET no inicio : 501 Not Implemented */
    else
    {
      size_strlen = strlen("HTTP/1.0 501 Not Implemented\r\n\r\n");        
      strncpy(mensagem, "HTTP/1.0 501 Not Implemented\r\n\r\n", size_strlen);
      envia_cliente(sock_cliente, mensagem, size_strlen);
    }
    
    printf("Encerrada conexao com socket %d\n", sock_cliente);
    close(sock_cliente);
  }
  
  else if (bytes < 0)
  {
    perror("recv()");
  }
  else if (bytes == 0)
  {
    printf("Socket %d encerrou a conexao.\n", sock_cliente);
  }
}

void envia_cliente (int sock_cliente, char mensagem[], int size_strlen)
{
  if (send(sock_cliente, mensagem, size_strlen, 0) == -1)
    perror("send()");
}

void recupera_caminho (char caminho[], char *pagina, char diretorio[])
{
  int tam_pagina = strlen(pagina);
  int tam_diretorio = strlen(diretorio);
  
  strncpy(caminho, diretorio, tam_diretorio);
  strncat(caminho, pagina, tam_pagina);
}

int existe_pagina (char *caminho)
{
  if (access(caminho, R_OK) == 0)
  {
    return 1; /* Arquivo existe com permissao para sua leitura */
  }
  return 0;
}

void formato_mesagem ()
{
  printf("Formato: ./recuperador <porta> <diretorio>\n");
  printf("Portas validas: 0 a 65535\n");
}

void recupera_arquivo (int sock_cliente, char *caminho)
{
  
  FILE *fp;
  char buf[BUFFERSIZE+1];
  char mensagem[BUFFERSIZE+1];
  int bytes_lidos;
  int size_strlen;
  
  if ((fp = fopen(caminho, "rb")) == NULL)
  {
    perror("fopen()");
    size_strlen = strlen("HTTP/1.0 404 Not Found\r\n\r\n");
    strncpy(mensagem, "HTTP/1.0 404 Not Found\r\n\r\n", size_strlen);
    envia_cliente(sock_cliente, mensagem, size_strlen);
    return;
  }
  else /* Envia o cabecalho */
  {
    size_strlen = strlen("HTTP/1.0 200 OK\r\n\r\n");
    strncpy(mensagem, "HTTP/1.0 200 OK\r\n\r\n", size_strlen);
    envia_cliente(sock_cliente, mensagem, size_strlen);
  }
  
  /* Enquanto nao tiver terminado de ler o arquivo, o envia para o cliente */
  while ((bytes_lidos = fread(buf, 1, sizeof(buf), fp)) > 0)
  {
    memcpy(mensagem, buf, bytes_lidos);
    envia_cliente(sock_cliente, mensagem, bytes_lidos);
  }
  fclose(fp);
}
