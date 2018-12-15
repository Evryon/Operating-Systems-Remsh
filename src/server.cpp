/**
 * @file server.cpp
 * @author Daniel Garrett (dgarrett3@fordham.edu)
 * @brief Start up a command server that listens for client 
 * connections and executes their shell commands. The server 
 * can handle multiple requests by fork()ing every new connection
 * and poll()ing the listening socket for new clients.
 * After every connection has been handled, the server automatically
 * shuts down.
 * @version 1.0
 * @date 2018-12-12
 * 
 * @copyright Copyright (c) 2018
 * 
 */

#include <netinet/in.h>  
#include <sys/socket.h>  
#include <sys/types.h> 
#include <sys/wait.h>
#include <sys/poll.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <csignal>
#include <memory>
#include <cerrno>

#include "vprint.h"
using namespace std;

/* Global Definitions */
const int BUFFERSIZE = 512; 
int serverpid, actvConns = 0; // ref count of connections
bool verboseOutput         = false, // extern declared in vprint.h
     stillHandlingRequests = true;  // flag to cancel main loop

/* Function Declarations */
void usage(const char *);
void sigchld_handler(int); // custom handler
void handle_request(int, char*, char*);

/* Main Program */
int main(int argc, char **argv)
{ 

  struct sigaction sigchldaction;
  struct addrinfo hints;
  struct addrinfo *addrHead = 0, *addr = 0;
  struct sockaddr_in cliAddr;
  struct timespec ts;
  sigset_t sigmask, emptymask;
  struct pollfd connectPFD;  
  unsigned long portAsLong;
  socklen_t cliAddrLen = sizeof(struct sockaddr_storage);
  int connectFD, sessFD, opt, pid, pollval;
  const char *port = "8888";

  serverpid = getpid();

  sigemptyset(&sigmask);
  sigemptyset(&emptymask);
  sigaddset(&sigmask, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, &sigmask, NULL) < 0) {
    perror("sigprocmask()");
    return EXIT_FAILURE;
  }

  memset(&sigchldaction, 0, sizeof (struct sigaction));
  sigfillset(&sigchldaction.sa_mask);
  sigchldaction.sa_flags = 0;
  sigchldaction.sa_handler = sigchld_handler;
  sigaction(SIGCHLD, &sigchldaction, NULL);  // enable handler for children

  if (argc > 1) {
    while ((opt = getopt(argc, argv, "vhp:")) != -1)
    {
      switch(opt) {
        case 'p': // set custom port
          portAsLong = strtoul(optarg,NULL,10);
          if (errno == ERANGE || portAsLong > 0xffff)
          {// in here if given a bad port argument
            cerr << "Bad port, got: " << optarg << '\n';
            cerr << flush;
            return EXIT_FAILURE;
          }
          else {
            port = optarg;
          }
          break;
        case 'h': // help message
          usage(argv[0]);
          return EXIT_SUCCESS; 
          break;
        case 'v':
          verboseOutput = true;
          break;
        case '?': // bad string in argv go here
          usage(argv[0]);
          return EXIT_SUCCESS; 
        default:
          abort(); // something bad happened with optarg
      }
    }
  } // finish option processing

  verboseprint("Starting server ...\n");
  verboseprint("Attempting to start service at port %s\n", port);
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM; 
  hints.ai_protocol = 0;       // IP or any protocol
  hints.ai_flags = AI_PASSIVE;

  int s = getaddrinfo(NULL, port, &hints, &addrHead);
  if (s != 0) {
    cerr << "Error in gettaddrinfo(): " << gai_strerror(s) << '\n';
    cerr << flush;
    return EXIT_FAILURE;
  }

  for( addr = addrHead;   // iterate through the
       addr != NULL;       // list of addresses 
       addr = addr->ai_next) // from getaddrinfo()
  {
    connectFD = socket(addr->ai_family,
                       addr->ai_socktype, 
                       addr->ai_protocol);
    if (connectFD == -1)
      continue;
    
    if (bind(connectFD, addr->ai_addr, addr->ai_addrlen) == 0)
      break; // succesfully bound socket to host
    
    close(connectFD); // bind(2) fails close socket
  }

  if (addr == NULL) {
    perror("Could not bind");
    //cerr << "Cound not bind. Try freeing up file descriptors for the socket.\n" << flush;
    freeaddrinfo(addrHead);
    return EXIT_FAILURE;
  }

  freeaddrinfo(addrHead);

  verboseprint("Service started. Listening on port %s\n", port);

  /* The original parent process is the main loop that will
     accept() connections and forks a process to handle it. */
  listen(connectFD,64); // server is listening

  // using ppoll() requires passing an pollfd struct, which
  // contains the polled file descriptor and a mask of 
  // events to listen for, specifically when a message is in.
  memset(&connectPFD, 0, sizeof (struct pollfd));
  connectPFD.fd = connectFD;
  connectPFD.events = POLLIN;
  ts.tv_sec = 3;

  for (;;) { // main loop
    pollval = ppoll(&connectPFD, 1, &ts, &emptymask);

    // use poll() to synchronously determine if the connection
    // socket has a client waiting to connect.
    if (errno == EINTR) break;
    if (pollval == -1) {
      perror("Error in ppoll()");
      break;
    } 
    else if (pollval > 0) {
      sessFD = accept(connectFD,
                     (struct sockaddr *) &cliAddr, &cliAddrLen);
      if (sessFD < 0) {
        perror("Accept");
        break;
      }
    
      unique_ptr<char> host(new char[NI_MAXHOST]),
                      service(new char[NI_MAXSERV]);
      s = getnameinfo((struct sockaddr *) &cliAddr, cliAddrLen,
                      host.get(), NI_MAXHOST,        // get client host
                      service.get(), NI_MAXSERV, 0); // get client port
      if (s != 0) {
        cerr << "Error in getnameinfo(): " << gai_strerror(s) << '\n' << flush;
        strcpy(host.get(), "UnkownHost");
        strcpy(service.get(), "UnkownPort");
      }

      verboseprint("Received connection from %s:%s.\n", host.get(), service.get());
      pid = fork();
      if (pid < 0) {
        cerr << "Could not fork process. Aborting.\n";
        abort();
      }
      else if (pid == 0)
      { // in child, so handle the connection
        handle_request(sessFD, host.release(), service.release());
        return EXIT_SUCCESS;
      }
      else {
        // parent will print number of connections if -v is 
        // selected, the continue back to select()

        verboseprint("Active connections: %d\n", ++actvConns);
      }
    } // end client handle_request() condition
    else {
      /* nothing interesting happened if select() returns 0
         In most cases, go back to the main loop an poll the 
         listening socket, but if all connections have teminated
         then the server can shut down. */
      if (stillHandlingRequests == false) break;
      continue;
    }
  }

  // use a waitpid(2) loop to mop up all child 
  // processes so no zombies are left roaming
  while ((pid = waitpid(WAIT_ANY, NULL, 0)) != 0) {
    if (errno == ECHILD) break;
  }
  verboseprint("Shutting down server.\n");
  close(connectFD);
  return 0;
}

void usage(const char *name)
{
  cout << name << ": [-p port] [-v]\n\n"
  << "Start a server that executes a user's shell commands when they connect. The\n"
  << "service can either run non-interactively, closing the connection after the batch\n"
  << "job has completed; or interactively and the user terminates the connection themselves.\n"
  << "Normally, the shell that will be reading the remote commands is /bin/bash on Linux\n"
  << "distributions, and /bin/sh on Unix.\n\n"
  << "\tOptions\tDescription\n"
  << "\t-p\tRun the server on the given port. It must be a decimal integer between 0-65535,\n"
  << "\t\t but it is good practive to select a port higher than 1024. Default is 8888.\n"
  << "\t-v\trun the server verbosely. It will print status messages to stdout.\n";
}

/**
 * @brief Because we are spawning child processes with forkpty
 * from a detached thread, the main thread will not block and 
 * accept new connections. The main thread will receive a SIGCHLD
 * when a proccess (also, connection) terminates so it can update
 * a counter on the number of active connections.
 * When the connections == 0, the server will stop handling 
 * requests and terminate.
 *  
 * @param signum a signal number, see <signals.h>
 */
void sigchld_handler(int signum)
{
  actvConns--;
  if (actvConns == 0)
    stillHandlingRequests = false;
}

/**
 * @brief To enable the server to handle multiple clients, every 
 * connection will be handled in its own child process. This function
 * encapsulated the routine of reading from the socket, executing the 
 * commands, and returning the output. Afterwards, the connection is
 * closed and the child process sends a kill(SIGCHLD) signal to the
 * server process, which keeps a reference count of all connections
 * and can decrement the counter after termination.
 * 
 * @param sessFD file descriptor pointing to an active client session
 * @param host hostname of the client
 * @param service port from which client connected
 */
void handle_request(int sessFD, char *host, char *service)
{
  struct pollfd sessPFD = {};
  struct timespec ts = {};
  sigset_t sigemptymask;
  int bytesRead, pid, pollval;

  sigemptyset(&sigemptymask);
  sessPFD.fd = sessFD;
  sessPFD.events = POLLHUP | POLLIN;
  ts.tv_sec = 3;
  
  for (;;) {// begin loop to handle multiple commands
    pollval = ppoll(&sessPFD, 1, &ts, &sigemptymask);

    if (pollval == -1 && errno != EINTR) {
      // something bad happened in ppoll(). Just quit.
      perror("Error with ppoll() in handle_request()");
      break;
    }
    else if (sessPFD.revents & POLLIN) {
      // Client has sent a command, then POLLIN is true
      char buffer[BUFFERSIZE+1] = {};
      bytesRead = recv(sessFD, buffer, BUFFERSIZE,0);
      if (bytesRead <= 0) {
        break;
      }

      buffer[bytesRead] = '\0'; // add null char to end
      verboseprint("Read from client was: %s\n", buffer);

      string result;
      shared_ptr<FILE> pipe(popen(buffer, "r"), pclose);

      if (!pipe) {
        perror("popen()");
        break;
      }

      memset(&buffer, 0, BUFFERSIZE+1);
      while (!feof(pipe.get())) {
          if (fgets(buffer, BUFFERSIZE, pipe.get()) != NULL)
              result.append(buffer);
      }

      int n = send(sessFD, result.c_str(), result.size()+1,0);
      if (n < 0) {
        perror("Error in write()");
        break;
      }
    } // end popen() and return output 
    else if (sessPFD.revents & POLLHUP) {
      // if client shutdown() the connection, the server can stop
      break;
    }
    else {
      continue; // poll returns 0 if nothing happens before timeout
    }
   } // end scope of polling session socket

  // Do not kill the connection until the shell exits
  verboseprint("Terminating connection from %s:%s\n", host, service);
  delete [] host; 
  delete [] service;
  shutdown(sessFD, SHUT_RDWR);
  close(sessFD);
  kill(serverpid, SIGCHLD);
}