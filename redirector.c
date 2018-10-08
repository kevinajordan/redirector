/*
 * redirector - Instantiates a listening socket to redirect connections to a remote
 * socket. 'l_port'(local port) accepts connections on localhost (or IP specified),
 * which will connect to 'r_port' (remote port) on 'r_ip' (remote ip).
 *
 *  Benefits of redirector:
 * - handles all processing from within a single process
 * - suitable for both Windows and Unix environments
 * - ideal for low memory environments since no forking happens.
 * - can speficy listening IP. Ideal for hosts with multiple IPs.
 *
 * Possible Improvements:
 * - using select()
 *
 * For Unix OSs, compile with:
 *     cc -O -o redirector redirector.c
 * For Windows, compile with:
 *     cl /W3 datapipe.c wsock32.lib      (Microsoft Visual C++)
 *
 * Usage:
 *   redirector l_ip l_port r_ip r_port
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32)
#else
  #include <sys/time.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/wait.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
  #include <strings.h>
  #define recv(x,y,z,a) read(x,y,z)
  #define send(x,y,z,a) write(x,y,z)
  #define closesocket(s) close(s)
  typedef int SOCKET;
#endif

#ifndef INADDR_NONE
#endif


struct client_t
{
  int inuse;
  SOCKET client_sock, out_sock;
  time_t activity;
};

#define MAXCLIENTS 20
#define IDLETIMEOUT 300

int main(int argc, char *argv[])
{ 
  SOCKET l_sock;
  char buf[4096];
  struct sockaddr_in laddr, out_addr;
  int i;
  struct client_t clients[MAXCLIENTS];


#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32)
  /* Winsock needs additional startup activities */
  WSADATA wsadata;
  WSAStartup(MAKEWORD(1,1), &wsadata);
#endif


  /* check command line args */
  if (argc != 5) {
    fprintf(stderr,"Usage: %s l_ip l_port r_ip r_port\n",argv[0]);
    return 30;
  }


  /* reset all of the client structures */
  for (i = 0; i < MAXCLIENTS; i++)
    clients[i].inuse = 0;


  /* determine the listener address and port */
  bzero(&laddr, sizeof(struct sockaddr_in));
  laddr.sin_family = AF_INET;
  laddr.sin_port = htons((unsigned short) atol(argv[2]));
  laddr.sin_addr.s_addr = inet_addr(argv[1]);
  if (!laddr.sin_port) {
    fprintf(stderr, "invalid listener port\n");
    return 20;
  }
  if (laddr.sin_addr.s_addr == INADDR_NONE) {
    struct hostent *n;
    if ((n = gethostbyname(argv[1])) == NULL) {
      perror("gethostbyname");
      return 20;
    }    
    bcopy(n->h_addr, (char *) &laddr.sin_addr, n->h_length);
  }


  /* get outgoing socket */
  bzero(&out_addr, sizeof(struct sockaddr_in));
  out_addr.sin_family = AF_INET;
  out_addr.sin_port = htons((unsigned short) atol(argv[4]));
  if (!out_addr.sin_port) {
    fprintf(stderr, "invalid target port\n");
    return 25;
  }
  out_addr.sin_addr.s_addr = inet_addr(argv[3]);
  if (out_addr.sin_addr.s_addr == INADDR_NONE) {
    struct hostent *n;
    if ((n = gethostbyname(argv[3])) == NULL) {
      perror("gethostbyname");
      return 25;
    }    
    bcopy(n->h_addr, (char *) &out_addr.sin_addr, n->h_length);
  }


  /* create listener socket */
  if ((l_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return 20;
  }
  if (bind(l_sock, (struct sockaddr *)&laddr, sizeof(laddr))) {
    perror("bind");
    return 20;
  }
  if (listen(l_sock, 5)) {
    perror("listen");
    return 20;
  }


  /* change port in the listener struct to zero for binding to outgoing local sockets. */
  laddr.sin_port = htons(0);


  /* fork off into the background. */
#if !defined(__WIN32__) && !defined(WIN32) && !defined(_WIN32)
  if ((i = fork()) == -1) {
    perror("fork");
    return 20;
  }
  if (i > 0)
    return 0;
  setsid();
#endif

  
  /* main polling loop. */
  while (1)
  {
    fd_set fdsr;
    int maxsock;
    struct timeval tv = {1,0};
    time_t now = time(NULL);

    /* build the list of sockets to check. */
    FD_ZERO(&fdsr);
    FD_SET(l_sock, &fdsr);
    maxsock = (int) l_sock;
    for (i = 0; i < MAXCLIENTS; i++)
      if (clients[i].inuse) {
        FD_SET(clients[i].client_sock, &fdsr);
        if ((int) clients[i].client_sock > maxsock)
          maxsock = (int) clients[i].client_sock;
        FD_SET(clients[i].out_sock, &fdsr);
        if ((int) clients[i].out_sock > maxsock)
          maxsock = (int) clients[i].out_sock;
      }      
    if (select(maxsock + 1, &fdsr, NULL, NULL, &tv) < 0) {
      return 30;
    }


    /* check if there are new connections to accept. */
    if (FD_ISSET(l_sock, &fdsr))
    {
      SOCKET client_sock = accept(l_sock, NULL, 0);
     
      for (i = 0; i < MAXCLIENTS; i++)
        if (!clients[i].inuse) break;
      if (i < MAXCLIENTS)
      {
        /* connect a socket to the outgoing socket */
        SOCKET out_sock;
        if ((out_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
          perror("socket");
          closesocket(client_sock);
        }
        else if (bind(out_sock, (struct sockaddr *)&laddr, sizeof(laddr))) {
          perror("bind");
          closesocket(client_sock);
          closesocket(out_sock);
        }
        else if (connect(out_sock, (struct sockaddr *)&out_addr, sizeof(out_addr))) {
          perror("connect");
          closesocket(client_sock);
          closesocket(out_sock);
        }
        else {
          clients[i].out_sock = out_sock;
          clients[i].client_sock = client_sock;
          clients[i].activity = now;
          clients[i].inuse = 1;
        }
      } else {
        fprintf(stderr, "too many clients\n");
        closesocket(client_sock);
      }        
    }


    /* service any client connections that have waiting data. */
    for (i = 0; i < MAXCLIENTS; i++)
    {
      int num_bytes, closeneeded = 0;
      if (!clients[i].inuse) {
        continue;
      } else if (FD_ISSET(clients[i].client_sock, &fdsr)) {
        if ((num_bytes = recv(clients[i].client_sock, buf, sizeof(buf), 0)) <= 0 ||
          send(clients[i].out_sock, buf, num_bytes, 0) <= 0) closeneeded = 1;
        else clients[i].activity = now;
      } else if (FD_ISSET(clients[i].out_sock, &fdsr)) {
        if ((num_bytes = recv(clients[i].out_sock, buf, sizeof(buf), 0)) <= 0 ||
          send(clients[i].client_sock, buf, num_bytes, 0) <= 0) closeneeded = 1;
        else clients[i].activity = now;
      } else if (now - clients[i].activity > IDLETIMEOUT) {
        closeneeded = 1;
      }
      if (closeneeded) {
        closesocket(clients[i].client_sock);
        closesocket(clients[i].out_sock);
        clients[i].inuse = 0;
      }      
    }
    
  }
  return 0;
}




