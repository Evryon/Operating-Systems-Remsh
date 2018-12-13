/**
 * @file client.cpp
 * @author Daniel Garrett (dgarrett3@fordham.edu)
 * @brief Start a session to a remote server and pass commands
 * that will be executed by a bash shell. The user can issue commands
 * non-interactively from the command line with the -c option, or
 * be prompted continually for commands until 'exit', which kills the 
 * connection.
 * 
 * @version 0.1
 * @date 2018-12-12
 * 
 * @copyright Copyright (c) 2018
 * 
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h> 
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <csignal>
#include <cerrno>
#include <string>

#include "vprint.h"

using namespace std;

static const unsigned BUFFERSIZE = 1024;
bool verboseOutput;
void usage(const char * const);

int main(int argc, char * const *argv)
{
  struct addrinfo hints;
  struct addrinfo *addr=0, *addrHead=0;
  int sessFD, opt, bytesread;
  unsigned long portAsLong;
  const char *host="127.0.0.1", *service="8888";
  bool noninteractive = false;
  
  verboseOutput = true;
  string command;

  if (argc < 2)
  {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  while((opt = getopt(argc, argv, ":vh:p:c:?")) != -1)
  {
    switch (opt) 
    {
      case 'h':
         host = optarg;
         break;
      case 'p':
          portAsLong = strtoul(optarg,NULL,10);
          if (errno == ERANGE || portAsLong > 0xffff)
          {// in here if given a bad port argument
            cerr << "Bad port, got: " << optarg << '\n';
            return EXIT_FAILURE; // BAD_ARG
          }
          else {
            service = optarg;
          }
          break;
      case 'c':
        command.assign(optarg);
        noninteractive = true;
        break;
      case 'v':
        verboseOutput = true;
        break;
      case '?':
        usage(argv[0]);
        return EXIT_SUCCESS;
      case ':':
        return EXIT_FAILURE;
      default:
        abort();
    }
  }

  verboseprint("Starting client ...\n");

  memset(&hints ,0,sizeof (struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_protocol = 0;
  hints.ai_socktype = SOCK_STREAM;

  int s = getaddrinfo(host, service, &hints, &addrHead);
  if (s != 0) {
    cerr << "Failed in getaddrinfo(). " << gai_strerror(s) << '\n';
    return EXIT_FAILURE;
  }

  verboseprint("Connecting to %s:%s ...\n", host, service);
  int i;
  for (addr=addrHead, i=1; addr != NULL; addr = addr->ai_next, i++) {
    sessFD = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    // socket() can fail for multiple reasons, but we should
    // still try all possible addresses from getaddrinfo()
    if (sessFD == -1)
      continue;

    verboseprint("\tAttempt %d ... ",i);
    // Try to connect to the remote server
    if (connect(sessFD, addr->ai_addr, addr->ai_addrlen) != -1)
    {
      verboseprint("Success.\n");
      break;
    }
    else {
      verboseprint("Failed.\n");
      close(sessFD); // failed to connect, so close socket
    }
  }

  // Once we have a fully connected socket, or we have exhasted
  // all addresses, returned by getaddrinfo() can be freed
  freeaddrinfo(addrHead);

  if (addr == NULL) { // no addresses worked, just quit
    cerr << "Could not connect to " << host << ':' << service << '\n';
    return EXIT_FAILURE;
  }

  /* Now that there is a connection to the server, 
     the client can start issuing commands. */

  for (;;) {
    int thisreadn=0;
    bytesread=0;
    char buffer[BUFFERSIZE+1] = {};

    if (!noninteractive) {
      cout << "$ ";
      getline(cin,command);
    }
    // terminate connection with 'exit'
    if (command == "exit") {
      break;
    }

    // send the command to the server
    if (write(sessFD, command.c_str(), command.size()+1) < 0) {
      perror("Error in write() to server");
      break;
    }

    for (;;) {
      memset(&buffer, 0, sizeof buffer);
      thisreadn = read(sessFD, buffer, BUFFERSIZE);
      if (thisreadn < 0) {
        cerr << "Did not read succesfully\n.";
        return EXIT_FAILURE; 
      }
      else if (thisreadn == 0) { 
        // if reading EOF, we're done
        break;
      }

      bytesread += thisreadn;
      buffer[thisreadn] = '\0';
      cout << buffer << '\n';

      // server appends null byte to end of bytes. If the last byte
      // read is the null byte, then the whole message was received
      if (buffer[thisreadn-1] == '\0') break;
    }
    verboseprint("Received response from server of %d bytes\n", bytesread); 
    if (noninteractive) break;
  }

  verboseprint("Shutting down client...\n");
  shutdown(sessFD, SHUT_RDWR); // send HUP signal
  close(sessFD);
  return EXIT_SUCCESS;
}

void usage(const char * const name)
{
  cerr << "Usage: " << name << " -h host\n"
    << "\t[-p port]\n"
    << "\t[-c command]\n"
    << "\nConnect to a remote shell server listening on the host and port.\n"
    << "The commands are ran in whatever the server's default shell is,\n"
    << "normally this is /bin/bash for most Linux distributions and /bin/sh\n"
    << "for Unix."
    << "\n\n\tOPTIONS  |  DESCRIPTION\n"
    << "\t-h\tthe address of the target server. This can either\n"
           << "\t\t  be a hostname or a numeric IP address.\n"
    << "\t-p\tthe port the service is running on. Defaulted to 8888.\n"
    << "\t-c\trun the shell non-interactively with a command.\n"
    << "\t-?\tdisplay this help message.\n";
}