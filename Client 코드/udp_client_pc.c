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

// 로그인 페이지 UI 실행 함수
void launch_ui() {
  pid_t pid = fork();
  if (pid == -1) {
    perror("❌ fork failed");
    exit(EXIT_FAILURE);
  }
  if (pid == 0) {
    // 자식 프로세스: loginpage 실행
    execlp("./loginpage", "loginpage", NULL);
    perror("❌ execlp failed");  // 실행 실패 시 출력
    exit(EXIT_FAILURE);
  }
}

// void handle_stdin_input() {
//   fd_set read_fds;
//   struct timeval timeout;
//   char command[32];

//   while (1) {
//     FD_ZERO(&read_fds);
//     FD_SET(STDIN_FILENO, &read_fds);
//     timeout.tv_sec = 1;
//     timeout.tv_usec = 0;

//     int ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);

//     if (ready > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
//       printf("🔹 [DEBUG] stdin에서 입력 감지됨\n");  // 추가 확인
//       fflush(stdout);

//       if (fgets(command, sizeof(command), stdin) != NULL) {
//         command[strcspn(command, "\n")] = '\0';  // 개행 문자 제거

//         printf("🔹 [DEBUG] 입력 감지됨: '%s'\n", command);
//         fflush(stdout);

//         login_command(command);  // `/ID`, `/PW`, `/Login` 처리
//       } else {
//         printf(
//             "❌ [ERROR] stdin에서 fgets()가 NULL을 반환함. 입력 문제 발생 "
//             "가능\n");
//       }
//     }
//   }
// }

// 서버로부터 문자를 받음
void *receive_messages(void *arg) {
  char recv_buf[MAX_BUF];
  struct sockaddr_in server_addr;
  socklen_t server_len = sizeof(server_addr);

  while (1) {
    memset(recv_buf, 0, MAX_BUF);
    int len = recvfrom(sfd, recv_buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&server_addr, &server_len);

    if (len > 0) {
      recv_buf[len] = '\0';
      printf("\n📩 [Chat] %s\n", recv_buf);  // 채팅 메시지 출력
      printf("[%s] ", nickname);             // 사용자 입력 줄 다시 출력
      fflush(stdout);
    }
  }
  return NULL;
}

// 회원가입 함수
void register_user() {
  char id[32], password[32], pw_check[32];

  // 📌 NewAccountpage 실행 (회원가입 UI)
  pid_t pid = fork();
  if (pid == -1) {
    perror("❌ fork failed");
    return;
  }
  if (pid == 0) {
    execlp("./NewAccountpage", "NewAccountpage", NULL);
    perror("❌ execlp failed");  // 실행 실패 시 오류 출력
    exit(EXIT_FAILURE);
  }

  printf("User registration:\n");
  while (1) {
    printf("ID: ");
    scanf("%s", id);
    getchar();
    printf("Password: ");
    scanf("%s", password);
    getchar();
    printf("Confirm Password: ");
    scanf("%s", pw_check);
    getchar();

    if (strcmp(password, pw_check) != 0) {
      printf("❌ Passwords do not match. Try again.\n");
      continue;
    }

    printf("Nickname: ");
    scanf("%s", nickname);
    getchar();

    // 서버로 회원가입 요청 전송
    snprintf(buf, MAX_BUF, "REGISTER %s %s %s", id, password, nickname);
    send_request(buf);

    // 서버 응답 확인 후 성공 시 루프 종료
    if (strstr(buf, "successful")) {
      break;
    }
  }
}

// ✅ 서버로 로그인 요청 보내는 함수
void login_command(char *command) {
  static char id[32] = "";
  static char password[32] = "";

  if (strcmp(command, "/ID") == 0) {
    printf("🔹 [DEBUG] /ID 명령어 감지됨\n");
    fflush(stdout);
    printf("ID : ");
    fflush(stdout);
    scanf("%s", id);
    getchar();
    // 개행 문자 제거 (버퍼 클리어)
  } else if (strcmp(command, "/PW") == 0) {
    printf("PW : ");
    scanf("%s", password);
    getchar();
  } else if (strcmp(command, "/Login") == 0) {
    if (strlen(id) == 0 || strlen(password) == 0) {
      printf("❌ ID와 비밀번호를 모두 입력해야 합니다.\n");
      return;
    }

    snprintf(buf, MAX_BUF, "LOGIN %s %s", id, password);
    sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
           sizeof(addr_server));

    // 서버 응답 확인
    memset(buf, 0, MAX_BUF);
    int len = recvfrom(sfd, buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&addr_server, &addr_server_len);
    if (len > 0) {
      buf[len] = '\0';
      printf("\n📩 [Server] %s\n", buf);
    }
  } else {
    printf("❌ 잘못된 명령어입니다. 다시 입력하세요.\n");
  }
}

// 챗 방 입장
void join_chat_room() {
  char room_name[32];
  scanf("%s", room_name);
  getchar();

  snprintf(buf, MAX_BUF, "JOIN_ROOM %s", room_name);
  send_request(buf);

  printf("📡 [CLIENT] Sent JOIN_ROOM request: %s\n", room_name);

  if (receive_response()) {
    if (strstr(buf, "Successfully joined")) {
      printf("✅ You have entered the chat room.\n");
    } else {
      printf("❌ Failed to join chat room.\n");
    }
  } else {
    printf("❌ [CLIENT] No response received from server.\n");
  }
}

// 채팅방 입장 및 메시지 전송
void chat_room(const char *room_name) {
  printf("\n💬 Entering chat room: %s\n", room_name);

  snprintf(buf, MAX_BUF, "JOIN_ROOM %s", room_name);
  sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
         sizeof(addr_server));

  // 📌 chatroompage 실행
  pid_t chat_pid = fork();
  if (chat_pid == -1) {
    perror("❌ fork failed");
    return;
  }
  if (chat_pid == 0) {
    execlp("./chatroompage", "chatroompage", NULL);
    perror("❌ execlp failed");  // 실행 실패 시 오류 출력
    exit(EXIT_FAILURE);
  }

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, receive_messages, NULL);

  while (1) {
    printf("[%s] ", nickname);
    fgets(buf, MAX_BUF, stdin);
    buf[strcspn(buf, "\n")] = '\0';  // 개행 문자 제거

    if (strlen(buf) == 0) {
      continue;
    }

    // ✅ `/exit` 입력 시 로비로 이동
    if (strcmp(buf, "/exit") == 0) {
      printf("🚪 Leaving chat room...\n");

      // 🔥 서버에 클라이언트 퇴장 알림 (LEAVE 요청)
      snprintf(buf, MAX_BUF, "LEAVE %s", nickname);
      sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
             sizeof(addr_server));

      pthread_cancel(recv_thread);  // 메시지 수신 스레드 종료

      // ✅ chatroom UI 종료
      kill(chat_pid, SIGTERM);     // 실행 중인 chatroompage 종료
      waitpid(chat_pid, NULL, 0);  // 자식 프로세스 종료 대기

      // ✅ lobbypage 실행
      pid_t lobby_pid = fork();
      if (lobby_pid == -1) {
        perror("❌ fork failed");
        return;
      }
      if (lobby_pid == 0) {
        execlp("./lobbypage", "lobbypage", NULL);
        perror("❌ execlp failed");  // 실행 실패 시 오류 출력
        exit(EXIT_FAILURE);
      }

      lobby();  // ✅ 로비 함수 실행
      return;
    }

    // ✅ `/screen` 입력 시 chatroompage 종료 후 screenpage 실행
    if (strcmp(buf, "/screen") == 0) {
      printf("🚪 Switching to screen sharing mode...\n");

      // 🔥 현재 실행 중인 chatroompage 종료
      kill(chat_pid, SIGTERM);     // 실행 중인 chatroompage 종료
      waitpid(chat_pid, NULL, 0);  // chatroompage 종료 대기

      // ✅ screenpage 실행
      pid_t screen_pid = fork();
      if (screen_pid == -1) {
        perror("❌ fork failed");
        return;
      }
      if (screen_pid == 0) {
        execlp("./screenpage", "screenpage", NULL);
        perror("❌ execlp failed");  // 실행 실패 시 오류 출력
        exit(EXIT_FAILURE);
      }

      // ✅ screenpage가 종료될 때까지 대기
      waitpid(screen_pid, NULL, 0);

      printf("✅ Returning to chatroom...\n");
      // ✅ chatroompage 다시 실행
      pid_t chat_pid2 = fork();
      if (chat_pid2 == -1) {
        perror("❌ fork failed");
        return;
      }
      if (chat_pid2 == 0) {
        execlp("./chatroompage", "chatroompage", NULL);
        perror("❌ execlp failed");  // 실행 실패 시 오류 출력
        exit(EXIT_FAILURE);
      }

      chat_pid = chat_pid2;  // 새로운 chatroompage의 PID 저장
      continue;              // 다시 채팅 입력을 받을 수 있도록 반복문 유지
    }
  }
}

// 대기실 함수
void lobby() {
  int choice;
  char room_name[32];

  while (1) {
    printf("\n🔹 Welcome to the lobby, %s\n", nickname);
    printf(
        "1. 채팅방 목록 보기\n2. 접속 중인 사용자 목록 보기\n3. 채팅방 "
        "생성\n4. 채팅방 참가\n5. 종료\n선택: ");
    scanf("%d", &choice);
    getchar();  // 입력 버퍼 비우기

    if (choice == 1) {
      send_request("LIST_ROOMS");
    } else if (choice == 2) {
      send_request("LIST_USERS");
    } else if (choice == 3) {
      while (1) {
        printf("Enter new chat room name: ");
        scanf("%s", room_name);
        getchar();
        snprintf(buf, MAX_BUF, "CREATE_ROOM %s", room_name);
        send_request(buf);

        // 📌 중복된 방이 있으면 다시 입력받기
        if (strstr(buf, "already exists")) {
          printf("❌ Chat room name already exists. Try a different name.\n");
          continue;
        } else {
          break;
        }
      }
    } else if (choice == 4) {
      printf("Enter chat room name to join: ");
      scanf("%s", room_name);
      getchar();
      snprintf(buf, MAX_BUF, "JOIN_ROOM %s", room_name);
      send_request(buf);
      chat_room(room_name);  // 채팅방 입장
    } else if (choice == 5) {
      printf("🚪 Exiting client...\n");
      exit_client();
      return;
    } else {
      printf("❌ Invalid input. Try again.\n");
    }
  }
}

int main(int argc, char *argv[]) {
  // UDP 소켓 생성
  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sfd == -1) {
    perror("Socket creation failed");
    return EXIT_FAILURE;
  }

  // 서버 주소 설정
  memset(&addr_server, 0, sizeof(addr_server));
  addr_server.sin_family = AF_INET;
  addr_server.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &addr_server.sin_addr);
  addr_server_len = sizeof(addr_server);

  // 서버 응답 대기 시 타임아웃 설정 (5초)
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  // ✅ 종료 시 "LOGOUT" 메시지를 보내도록 신호 핸들러 설정
  signal(SIGINT, handle_exit);

  // ✅ PING 메시지를 보내는 스레드 시작
  pthread_t ping_thread;
  pthread_create(&ping_thread, NULL, (void *)send_ping, NULL);

  // 📌 초기 화면: 로그인 UI 실행
  launch_ui();
  printf("\n🔐 로그인 화면 (명령어 기반)\n");
  printf("명령어: /ID, /PW, /Login, /Register, /Quit\n");

  while (1) {
    char command[32];
    scanf("%s", command);

    if (strcmp(command, "/Register") == 0) {
      register_user();
      printf("📌 회원가입 완료! 다시 로그인해주세요.\n");
    } else if (strcmp(command, "/Quit") == 0) {
      printf("🚪 클라이언트를 종료합니다.\n");
      handle_exit(0);
    } else {
      login_command(command);  // 로그인 관련 명령어 처리
    }
  }

  close(sfd);
  return EXIT_SUCCESS;
}