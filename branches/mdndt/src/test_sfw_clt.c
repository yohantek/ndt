/*
 * This file contains the functions needed to handle simple firewall
 * test (client part).
 *
 * Jakub S�awi�ski 2006-07-15
 * jeremian@poczta.fm
 */

#include <assert.h>

#include "test_sfw.h"
#include "logging.h"
#include "protocol.h"
#include "network.h"
#include "utils.h"

static int c2s_result = SFW_NOTTESTED;
static int s2c_result = SFW_NOTTESTED;

/*
 * Function name: catch_alrm
 * Description: Prints the appropriate message when the SIGALRM is catched.
 * Arguments: signo - the signal number (shuld be SIGALRM)
 */

void
catch_alrm(int signo)
{
  if (signo == SIGALRM) {
    log_println(1, "SIGALRM was caught");
    return;
  }
  log_println(0, "Unknown (%d) signal was caught", signo);
}

/*
 * Function name: test_osfw_clt
 * Description: Performs the client part of the opposite Simple
 *              firewall test.
 * Arguments: ctlsockfd - the server control socket descriptor
 *            conn_options - the connection options
 */

void
test_osfw_clt(int ctlsockfd, int testTime, int conn_options)
{
  char buff[BUFFSIZE+1];
  I2Addr sfwcli_addr = NULL;
  int sfwsockfd, sfwsockport, sockfd;
  fd_set fds;
  struct timeval sel_tv;
  int msgLen, msgType;
  struct sockaddr_storage srv_addr;
  socklen_t srvlen;

  sfwcli_addr = CreateListenSocket(NULL, "0", conn_options);
  if (sfwcli_addr == NULL) {
    err_sys("client: CreateListenSocket failed");
  }
  sfwsockfd = I2AddrFD(sfwcli_addr);
  sfwsockport = I2AddrPort(sfwcli_addr);
  log_println(1, "  -- oport: %d", sfwsockport);

  sprintf(buff, "%d", sfwsockport);
  send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));

  FD_ZERO(&fds);
  FD_SET(sfwsockfd, &fds);
  sel_tv.tv_sec = testTime;
  sel_tv.tv_usec = 0;
  switch (select(sfwsockfd+1, &fds, NULL, NULL, &sel_tv)) {
    case -1:
      log_println(0, "Simple firewall test: select exited with error");
      I2AddrFree(sfwcli_addr);
      return;
    case 0:
      log_println(1, "Simple firewall test: no connection for %d seconds", testTime);
      s2c_result = SFW_POSSIBLE;
      I2AddrFree(sfwcli_addr);
      return;
  }
  srvlen = sizeof(srv_addr);
  sockfd = accept(sfwsockfd, (struct sockaddr *) &srv_addr, &srvlen);

  msgLen = sizeof(buff);
  if (recv_msg(sockfd, &msgType, &buff, &msgLen)) {
    log_println(0, "Simple firewall test: unrecognized message");
    s2c_result = SFW_UNKNOWN;
    close(sockfd);
    I2AddrFree(sfwcli_addr);
    return;
  }
  if (check_msg_type("Simple firewall test", TEST_MSG, msgType)) {
    s2c_result = SFW_UNKNOWN;
    close(sockfd);
    I2AddrFree(sfwcli_addr);
    return;
  }
  if (msgLen != 20) {
    log_println(0, "Simple firewall test: Improper message");
    s2c_result = SFW_UNKNOWN;
    close(sockfd);
    I2AddrFree(sfwcli_addr);
    return;
  }
  buff[msgLen] = 0;
  if (strcmp(buff, "Simple firewall test") != 0) {
    log_println(0, "Simple firewall test: Improper message");
    s2c_result = SFW_UNKNOWN;
    close(sockfd);
    I2AddrFree(sfwcli_addr);
    return;
  }

  s2c_result = SFW_NOFIREWALL;
  close(sockfd);
  I2AddrFree(sfwcli_addr);
}

/*
 * Function name: test_sfw_clt
 * Description: Performs the client part of the Simple firewall test.
 * Arguments: ctlsockfd - the server control socket descriptor
 *            tests - the set of tests to perform
 *            host - the hostname of the server
 *            conn_options - the connection options
 * Returns: 0 - success (the test has been finalized).
 */

int
test_sfw_clt(int ctlsockfd, char tests, char* host, int conn_options)
{
  char buff[BUFFSIZE+1];
  int msgLen, msgType;
  int sfwport, sfwsock;
  int testTime;
  I2Addr sfwsrv_addr = NULL;
  struct sigaction new, old;
  char* ptr;
  
  if (tests & TEST_SFW) {
    log_println(1, " <-- Simple firewall test -->");
    printf("checking for firewalls . . . . . . . . . . . . . . . . . . .  ");
    fflush(stdout);
    msgLen = sizeof(buff);
    if (recv_msg(ctlsockfd, &msgType, &buff, &msgLen)) {
      log_println(0, "Protocol error!");
      exit(1);
    }
    if (check_msg_type("Simple firewall test", TEST_PREPARE, msgType)) {
      exit(2);
    }
    if (msgLen <= 0) {
      log_println(0, "Improper message");
      exit(3);
    }
    buff[msgLen] = 0;
    ptr = strtok(buff, " ");
    if (ptr == NULL) {
      log_println(0, "SFW: Improper message");
      exit(5);
    }
    if (check_int(ptr, &sfwport)) {
      log_println(0, "Invalid port number");
      exit(4);
    }
    ptr = strtok(NULL, " ");
    if (ptr == NULL) {
      log_println(0, "SFW: Improper message");
      exit(5);
    }
    if (check_int(ptr, &testTime)) {
      log_println(0, "Invalid waiting time");
      exit(4);
    }
    log_println(1, "  -- port: %d", sfwport);
    log_println(1, "  -- time: %d", testTime);
    if ((sfwsrv_addr = I2AddrByNode(NULL, host)) == NULL) {
      perror("Unable to resolve server address\n");
      exit(-3);
    }
    I2AddrSetPort(sfwsrv_addr, sfwport);

    /* ignore the alrm signal */
    memset(&new, 0, sizeof(new));
    new.sa_handler = catch_alrm;
    sigaction(SIGALRM, &new, &old);
    alarm(testTime + 1);
    if (CreateConnectSocket(&sfwsock, NULL, sfwsrv_addr, conn_options) == 0) {
      send_msg(sfwsock, TEST_MSG, "Simple firewall test", 20);
    }
    alarm(0);
    sigaction(SIGPIPE, &old, NULL);

    msgLen = sizeof(buff);
    if (recv_msg(ctlsockfd, &msgType, &buff, &msgLen)) {
      log_println(0, "Protocol error!");
      exit(1);
    }
    if (check_msg_type("Simple firewall test", TEST_MSG, msgType)) {
      exit(2);
    }
    if (msgLen <= 0) {
      log_println(0, "Improper message");
      exit(3);
    }
    buff[msgLen] = 0;
    if (check_int(buff, &c2s_result)) {
      log_println(0, "Invalid test result");
      exit(4);
    }

    test_osfw_clt(ctlsockfd, testTime, conn_options);
    
    printf("Done\n");
    
    msgLen = sizeof(buff);
    if (recv_msg(ctlsockfd, &msgType, &buff, &msgLen)) {
      log_println(0, "Protocol error!");
      exit(1);
    }
    if (check_msg_type("Simple firewall test", TEST_FINALIZE, msgType)) {
      exit(2);
    }
    log_println(1, " <-------------------------->");
  }
  return 0;
}

/*
 * Function name: results_sfw
 * Description: Prints the results of the Simple firewall test to the client.
 * Arguments: tests - the set of tests to perform
 *            host - the hostname of the server
 * Returns: 0 - success.
 */

int
results_sfw(char tests, char* host)
{
  if (tests & TEST_SFW) {
    switch (c2s_result) {
      case SFW_NOFIREWALL:
        printf("Server '%s' is not behind a firewall.\n", host);
        break;
      case SFW_POSSIBLE:
#if 0
        printf("Server '%s' is probably behind a firewall.\n", host);
#endif
        break;
      case SFW_UNKNOWN:
      case SFW_NOTTESTED:
        break;
    }
    switch (s2c_result) {
      case SFW_NOFIREWALL:
        printf("Client is not behind a firewall.\n");
        break;
      case SFW_POSSIBLE:
#if 0
        printf("Client is probably behind a firewall.\n");
#endif
        break;
      case SFW_UNKNOWN:
      case SFW_NOTTESTED:
        break;
    }
  }
  return 0;
}
