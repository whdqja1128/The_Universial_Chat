#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "define.h"

#define MAX_BUF 256

int sfd;
struct sockaddr_in addr_server;
socklen_t addr_server_len;
char buf[MAX_BUF];
char nickname[32];  // 닉네임 저장 변수

// 서버에 클라이언트 접속 종료 알림
void exit_client() {
  snprintf(buf, MAX_BUF, "LEAVE %s", nickname);
  sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
         sizeof(addr_server));

  close(sfd);
  exit(EXIT_SUCCESS);
}

// 강제 종료시에도 나간 것을 알림
void handle_exit(int sig) {
  printf("\n🚪 [CLIENT] Logging out...\n");
  snprintf(buf, MAX_BUF, "LOGOUT %s", nickname);
  sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
         sizeof(addr_server));
  close(sfd);
  exit(0);
}

// 서버로 요청을 보내는 함수
void send_request(const char *request) {
  sendto(sfd, request, strlen(request), 0, (struct sockaddr *)&addr_server,
         sizeof(addr_server));
  receive_response();  // 응답 수신
}

// 서버 응답을 기다리는 함수
int receive_response() {
  memset(buf, 0, MAX_BUF);
  int len = recvfrom(sfd, buf, MAX_BUF - 1, 0, (struct sockaddr *)&addr_server,
                     &addr_server_len);

  if (len < 0) {
    perror("❌ recvfrom failed");  // 🔍 recvfrom() 에러 로그 추가
    printf("❌ No response from server. Connection issue or timeout.\n");
    return 0;
  }

  buf[len] = '\0';
  printf("\n📩 [Server] %s\n", buf);
  return 1;
}

// 클라이언트 존재 확인 용 신호 전송
void *send_ping(void *arg) {
    while (1) {
      sleep(10);  // ✅ 10초마다 PING 메시지 전송
      snprintf(buf, MAX_BUF, "PING %s", nickname);
      sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
             sizeof(addr_server));
    }
    return NULL;
  }