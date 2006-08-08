/*
 * This file contains the functions needed to handle simple firewall
 * test (server part).
 *
 * Jakub S�awi�ski 2006-07-15
 * jeremian@poczta.fm
 */

#include <assert.h>
#include <pthread.h>

#include "test_sfw.h"
#include "logging.h"
#include "protocol.h"
#include "network.h"
#include "utils.h"
#include "testoptions.h"

static pthread_mutex_t mainmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t maincond = PTHREAD_COND_INITIALIZER;
static I2Addr sfwcli_addr = NULL;
static int testTime = 30;
static int toWait = 1;

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
 * Function name: test_osfw_srv
 * Description: Performs the server part of the opposite Simple
 *              firewall test in the separate thread.
 */

void*
test_osfw_srv(void* vptr)
{
  int sfwsock;
  struct sigaction new, old;

  /* ignore the alrm signal */
  memset(&new, 0, sizeof(new));
  new.sa_handler = catch_alrm;
  sigaction(SIGALRM, &new, &old);
  alarm(testTime + 1);
  if (CreateConnectSocket(&sfwsock, NULL, sfwcli_addr, 0) == 0) {
    send_msg(sfwsock, TEST_MSG, "Simple firewall test", 20);
  }
  alarm(0);
  sigaction(SIGALRM, &old, NULL);

  pthread_mutex_lock( &mainmutex);
  toWait = 0;
  pthread_cond_broadcast(&maincond);
  pthread_mutex_unlock( &mainmutex);

  return NULL;
}

/*
 * Function name: finalize_sfw
 * Description: Waits for the every thread to accomplish and finalizes
 *              the SFW test.
 * Arguments: ctlsockfd - the client control socket descriptor
 */

void
finalize_sfw(int ctlsockfd)
{
  while (toWait) {
    pthread_mutex_lock( &mainmutex);
    pthread_cond_wait(&maincond, &mainmutex);
    pthread_mutex_unlock( &mainmutex);
  }
  send_msg(ctlsockfd, TEST_FINALIZE, "", 0);
  log_println(1, " <-------------------------->");
  setCurrentTest(TEST_NONE);
}

/*
 * Function name: test_sfw_srv
 * Description: Performs the server part of the Simple firewall test.
 * Arguments: ctlsockfd - the client control socket descriptor
 *            options - the test options
 *            conn_options - the connection options
 * Returns: 0 - success (no firewalls on the path),
 *          1 - failure (protocol mismatch),
 *          2 - unknown (probably firwall on the path).
 */

int
test_sfw_srv(int ctlsockfd, web100_agent* agent, TestOptions* options, int conn_options)
{
  char buff[BUFFSIZE+1];
  I2Addr sfwsrv_addr = NULL;
  int sfwsockfd, sfwsockport, sockfd, sfwport;
  struct sockaddr_storage cli_addr;
  socklen_t clilen;
  fd_set fds;
  struct timeval sel_tv;
  int msgLen, msgType;
  web100_var* var;
  web100_connection* cn;
  web100_group* group;
  int maxRTT, maxRTO;
  pthread_t threadId;
  char hostname[256];
  
  assert(ctlsockfd != -1);
  assert(options);

  if (options->sfwopt) {
    setCurrentTest(TEST_SFW);
    log_println(1, " <-- Simple firewall test -->");
    
    sfwsrv_addr = CreateListenSocket(NULL, "0", conn_options);
    if (sfwsrv_addr == NULL) {
      err_sys("server: CreateListenSocket failed");
    }
    sfwsockfd = I2AddrFD(sfwsrv_addr);
    sfwsockport = I2AddrPort(sfwsrv_addr);
    log_println(1, "  -- port: %d", sfwsockport);
    
    cn = web100_connection_from_socket(agent, ctlsockfd);
    if (cn) {
      web100_agent_find_var_and_group(agent, "RemAddress", &group, &var);
      web100_raw_read(var, cn, buff);
      memset(hostname, 0, 256);
      strncpy(hostname, web100_value_to_text(web100_get_var_type(var), buff), 255);
      web100_agent_find_var_and_group(agent, "MaxRTT", &group, &var);
      web100_raw_read(var, cn, buff);
      maxRTT = atoi(web100_value_to_text(web100_get_var_type(var), buff));
      web100_agent_find_var_and_group(agent, "MaxRTO", &group, &var);
      web100_raw_read(var, cn, buff);
      maxRTO = atoi(web100_value_to_text(web100_get_var_type(var), buff));
      if (maxRTT > maxRTO)
        maxRTO = maxRTT;
      if (((((double) maxRTO) / 1000.0) + 1) < 30.0)
        testTime = ((double) maxRTO) / 1000.0 + 1;
    }
    else {
      log_println(0, "Simple firewall test: Cannot find connection");
      exit(-1);
    }
    log_println(1, "  -- time: %d", testTime);
    
    sprintf(buff, "%d %d", sfwsockport, testTime);
    send_msg(ctlsockfd, TEST_PREPARE, buff, strlen(buff));
   
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
    if (check_int(buff, &sfwport)) {
      log_println(0, "Invalid port number");
      exit(4);
    }

    if ((sfwcli_addr = I2AddrByNode(NULL, hostname)) == NULL) {
      log_println(0, "Unable to resolve server address");
      send_msg(ctlsockfd, TEST_FINALIZE, "", 0);
      log_println(1, " <-------------------------->");
      exit(5);
    }
    I2AddrSetPort(sfwcli_addr, sfwport);
    log_println(1, "  -- oport: %d", sfwport);
    
    send_msg(ctlsockfd, TEST_START, "", 0);
    pthread_create(&threadId, NULL, &test_osfw_srv, NULL);

    FD_ZERO(&fds);
    FD_SET(sfwsockfd, &fds);
    sel_tv.tv_sec = testTime + 1;
    sel_tv.tv_usec = 0;
    switch (select(sfwsockfd+1, &fds, NULL, NULL, &sel_tv)) {
      case -1:
        log_println(0, "Simple firewall test: select exited with error");
        sprintf(buff, "%d", SFW_UNKNOWN);
        send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
        I2AddrFree(sfwsrv_addr);
        finalize_sfw(ctlsockfd);
        return 1;
      case 0:
        log_println(0, "Simple firewall test: no connection for %d seconds", testTime);
        sprintf(buff, "%d", SFW_POSSIBLE);
        send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
        I2AddrFree(sfwsrv_addr);
        finalize_sfw(ctlsockfd);
        return 2;
    }
    clilen = sizeof(cli_addr);
    sockfd = accept(sfwsockfd, (struct sockaddr *) &cli_addr, &clilen);

    msgLen = sizeof(buff);
    if (recv_msg(sockfd, &msgType, &buff, &msgLen)) {
      log_println(0, "Simple firewall test: unrecognized message");
      sprintf(buff, "%d", SFW_UNKNOWN);
      send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
      close(sockfd);
      I2AddrFree(sfwsrv_addr);
      finalize_sfw(ctlsockfd);
      return 1;
    }
    if (check_msg_type("Simple firewall test", TEST_MSG, msgType)) {
      sprintf(buff, "%d", SFW_UNKNOWN);
      send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
      close(sockfd);
      I2AddrFree(sfwsrv_addr);
      finalize_sfw(ctlsockfd);
      return 1;
    }
    if (msgLen != 20) {
      log_println(0, "Simple firewall test: Improper message");
      sprintf(buff, "%d", SFW_UNKNOWN);
      send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
      close(sockfd);
      I2AddrFree(sfwsrv_addr);
      finalize_sfw(ctlsockfd);
      return 1;
    }
    buff[msgLen] = 0;
    if (strcmp(buff, "Simple firewall test") != 0) {
      log_println(0, "Simple firewall test: Improper message");
      sprintf(buff, "%d", SFW_UNKNOWN);
      send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
      close(sockfd);
      I2AddrFree(sfwsrv_addr);
      finalize_sfw(ctlsockfd);
      return 1;
    }
    
    sprintf(buff, "%d", SFW_NOFIREWALL);
    send_msg(ctlsockfd, TEST_MSG, buff, strlen(buff));
    close(sockfd);
    I2AddrFree(sfwsrv_addr);
    finalize_sfw(ctlsockfd);
  }
  return 0;
}