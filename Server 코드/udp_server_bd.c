#include <arpa/inet.h>
#include <errno.h>
#include <mysql/mysql.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "define.h"

#define MAX_BUF 256
#define MAX_CLIENTS 25                   // 최대 클라이언트 수
#define CHAT_HISTORY_DIR "./chat_logs/"  // 채팅 로그를 저장할 디렉토리
#define MAX_CHAT_ROOMS 25

MYSQL *conn;
int sfd;

typedef struct {
  char name[32];
  char owner[32];  // 방장 정보
} ChatRoom;

ChatRoom chat_rooms[MAX_CHAT_ROOMS];  // 채팅방 목록
int chat_room_count = 0;              // 현재 존재하는 채팅방 개수

// 클라이언트 목록 저장
struct Client {
  struct sockaddr_in addr;
  char nickname[32];
  char room[32];  // 클라이언트가 속한 채팅방
  int active;
  time_t last_response;  // ✅ 최근 응답 시간 추가
};

struct Client clients[MAX_CLIENTS];
int client_count = 0;

// 채팅방 목록 저장
int room_count = 0;

// 🔹 데이터베이스 연결 함수
void connect_db() {
  conn = mysql_init(NULL);
  if (!conn) {
    printf("MySQL Initialization failed. Server will continue running.\n");
    return;
  }

  conn = mysql_real_connect(conn, "localhost", "chatuser", "your_password",
                            "db_chat", 0, NULL, 0);

  if (!conn) {
    printf("Database connection failed: %s\n", mysql_error(conn));
    printf("Server will continue running without database access.\n");
    conn = NULL;
    return;
  }

  printf("✅ Connected to MariaDB successfully!\n");
}

// 🔹 서버 응답 전송 함수
void send_response(struct sockaddr_in client_addr, const char *message) {
  sendto(sfd, message, strlen(message), 0, (struct sockaddr *)&client_addr,
         sizeof(client_addr));
}

// 🔹 회원가입 처리
void register_user(char *id, char *password, char *nickname,
                   struct sockaddr_in client_addr) {
  if (!conn) {
    send_response(client_addr, "Database unavailable.");
    return;
  }

  char query[MAX_BUF];
  snprintf(query, sizeof(query),
           "SELECT user_id FROM chat_user WHERE user_id='%s';", id);
  if (mysql_query(conn, query) == 0) {
    MYSQL_RES *result = mysql_store_result(conn);
    if (mysql_num_rows(result) > 0) {
      send_response(client_addr, "Registration failed: ID already exists.");
      mysql_free_result(result);
      return;
    }
    mysql_free_result(result);
  }

  snprintf(
      query, sizeof(query),
      "INSERT INTO chat_user (user_id, pw, name) VALUES ('%s', '%s', '%s');",
      id, password, nickname);
  if (mysql_query(conn, query) == 0) {
    send_response(client_addr, "Registration successful! Please login.");
  } else {
    send_response(client_addr, "Registration failed.");
  }
}

// 🔹 로그인 처리
void authenticate_user(char *id, char *password,
                       struct sockaddr_in client_addr) {
  if (!conn) {
    send_response(client_addr, "Database unavailable.");
    return;
  }

  char query[MAX_BUF];
  snprintf(query, sizeof(query),
           "SELECT name FROM chat_user WHERE user_id='%s' AND pw='%s';", id,
           password);

  if (mysql_query(conn, query) == 0) {
    MYSQL_RES *result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
      // **중복 로그인 체크**
      for (int i = 0; i < client_count; i++) {
        if (clients[i].active && strcmp(clients[i].nickname, row[0]) == 0) {
          send_response(
              client_addr,
              "Duplicate login detected. You are already logged in.");
          mysql_free_result(result);
          return;
        }
      }

      // **로그인 성공 처리**
      send_response(client_addr, "Login successful!");

      // **새로운 클라이언트를 목록에 추가**
      if (client_count < MAX_CLIENTS) {
        strcpy(clients[client_count].nickname, row[0]);
        clients[client_count].addr = client_addr;
        clients[client_count].active = 1;
        clients[client_count].last_response = time(NULL);
        client_count++;
      }
    } else {
      send_response(client_addr, "Login failed: Incorrect ID or password.");
    }
    mysql_free_result(result);
  }
}

// 클라이언트 나가는 것 체크
void remove_client(struct sockaddr_in client_addr) {
  for (int i = 0; i < client_count; i++) {
    if (memcmp(&clients[i].addr, &client_addr, sizeof(client_addr)) == 0) {
      printf("클라이언트 [%s] 나감, 목록에서 제거\n", clients[i].nickname);

      // **🌟 로그아웃 메시지 전송**
      char logout_msg[MAX_BUF];
      snprintf(logout_msg, MAX_BUF, "🚪 User [%s] has logged out.",
               clients[i].nickname);
      send_response(client_addr, logout_msg);

      // **🌟 클라이언트 리스트에서 제거**
      clients[i] = clients[client_count - 1];
      client_count--;
      return;
    }
  }
}

// 30초 이상 ping 안보내면 삭제제
void check_client_activity() {
  time_t now = time(NULL);
  for (int i = 0; i < client_count; i++) {
    if (clients[i].active && (now - clients[i].last_response > 30)) {
      printf("[SERVER] 클라이언트 %s 자동 로그아웃 (타임아웃)\n",
             clients[i].nickname);
      clients[i].active = 0;
      memset(&clients[i], 0, sizeof(struct Client));
      client_count--;
    }
  }
}

// 🔹 채팅방 생성
void create_chat_room(const char *room_name, const char *owner,
                      struct sockaddr_in client_addr) {
  if (chat_room_count >= MAX_CHAT_ROOMS) {
    send_response(client_addr, "채팅방 개수가 최대 한도에 도달했습니다.");
    return;
  }

  for (int i = 0; i < chat_room_count; i++) {
    if (strcmp(chat_rooms[i].name, room_name) == 0) {
      send_response(client_addr, "이미 존재하는 채팅방입니다.");
      return;
    }
  }

  strcpy(chat_rooms[chat_room_count].name, room_name);
  strcpy(chat_rooms[chat_room_count].owner, owner);
  chat_room_count++;

  // ✅ 데이터베이스에 채팅방 정보 저장
  if (conn) {
    char query[MAX_BUF];
    snprintf(query, sizeof(query),
             "INSERT INTO chat_room (name, owner) VALUES ('%s', '%s');",
             room_name, owner);
    if (mysql_query(conn, query) == 0) {
      send_response(client_addr, "채팅방이 성공적으로 생성되었습니다.");
    } else {
      send_response(client_addr, "채팅방 생성 실패: 데이터베이스 오류.");
    }
  }

  printf("[SERVER] 채팅방 생성됨: %s (방장: %s)\n", room_name, owner);
}

// 채팅방 삭제
void delete_chat_room(const char *room_name, const char *requester,
                      struct sockaddr_in client_addr) {
  int room_index = -1;

  // 1. 해당 채팅방이 존재하는지 확인
  for (int i = 0; i < chat_room_count; i++) {
    if (strcmp(chat_rooms[i].name, room_name) == 0) {
      room_index = i;
      break;
    }
  }

  if (room_index == -1) {
    send_response(client_addr, "존재하지 않는 채팅방입니다.");
    return;
  }

  // 2. 방장인지 확인 (방장만 삭제 가능)
  if (strcmp(chat_rooms[room_index].owner, requester) != 0) {
    send_response(client_addr, "방장만 채팅방을 삭제할 수 있습니다.");
    return;
  }

  // 3. DB에서 채팅방 삭제
  if (conn) {
    char query[MAX_BUF];
    snprintf(query, sizeof(query), "DELETE FROM chat_room WHERE name='%s';",
             room_name);
    if (mysql_query(conn, query) != 0) {
      send_response(client_addr, "채팅방 삭제 실패: 데이터베이스 오류");
      return;
    }
  }

  // 4. 채팅 내역 파일 삭제
  char filename[MAX_BUF];
  snprintf(filename, sizeof(filename), "chat_logs/%s.txt", room_name);
  if (remove(filename) == 0) {
    printf("[SERVER] 채팅방 '%s'의 기록이 삭제되었습니다.\n", room_name);
  } else {
    perror("채팅 내역 파일 삭제 실패");
  }

  // 5. 서버 메모리에서 채팅방 삭제 (배열에서 제거)
  for (int i = room_index; i < chat_room_count - 1; i++) {
    chat_rooms[i] = chat_rooms[i + 1];
  }
  chat_room_count--;

  send_response(client_addr, "채팅방이 성공적으로 삭제되었습니다.");
  printf("[SERVER] 채팅방 삭제됨: %s\n", room_name);
}

// 채팅방 룸 가져오
void load_chat_rooms() {
  char query[] = "SELECT name, owner FROM chat_room;";
  if (mysql_query(conn, query) != 0) {
    perror("채팅방 목록 불러오기 실패");
    return;
  }

  MYSQL_RES *result = mysql_store_result(conn);
  MYSQL_ROW row;

  chat_room_count = 0;  // 기존 채팅방 목록 초기화
  while ((row = mysql_fetch_row(result))) {
    if (chat_room_count >= MAX_CHAT_ROOMS) break;  // 최대 개수 초과 방지

    strncpy(chat_rooms[chat_room_count].name, row[0],
            sizeof(chat_rooms[0].name) - 1);
    strncpy(chat_rooms[chat_room_count].owner, row[1],
            sizeof(chat_rooms[0].owner) - 1);
    chat_rooms[chat_room_count].name[sizeof(chat_rooms[0].name) - 1] = '\0';
    chat_rooms[chat_room_count].owner[sizeof(chat_rooms[0].owner) - 1] = '\0';

    printf("채팅방 로드됨: %s (방장: %s)\n", chat_rooms[chat_room_count].name,
           chat_rooms[chat_room_count].owner);
    chat_room_count++;
  }
  mysql_free_result(result);
}

// 참가자 수 증가
void join_chat_room(const char *room_name, struct sockaddr_in client_addr) {
  char query[MAX_BUF];

  // 채팅방이 존재하는지 확인
  snprintf(query, sizeof(query),
           "SELECT name FROM chat_room WHERE name = '%s';", room_name);
  if (mysql_query(conn, query) != 0) {
    send_response(client_addr, "존재하지 않는 채팅방입니다.");
    return;
  }

  MYSQL_RES *result = mysql_store_result(conn);
  if (mysql_num_rows(result) == 0) {
    send_response(client_addr, "존재하지 않는 채팅방입니다.");
    mysql_free_result(result);
    return;
  }
  mysql_free_result(result);

  // 참가자 수 증가
  snprintf(query, sizeof(query),
           "UPDATE chat_room SET participant_count = participant_count + 1 "
           "WHERE name = '%s';",
           room_name);
  mysql_query(conn, query);

  send_response(client_addr, "채팅방 참가 성공!");
}
// 🔹 채팅방 목록 전송
void list_chat_rooms(struct sockaddr_in client_addr) {
  char response[MAX_BUF] = "현재 채팅방 목록:\n";

  if (chat_room_count == 0) {
    strcat(response, "없음\n");
  } else {
    for (int i = 0; i < chat_room_count; i++) {
      char room_info[64];
      snprintf(room_info, sizeof(room_info), "%d. %s (방장: %s)\n", i + 1,
               chat_rooms[i].name, chat_rooms[i].owner);
      strcat(response, room_info);
    }
  }

  send_response(client_addr, response);
}

// 🔹 접속 중인 사용자 목록 전송
void list_users(struct sockaddr_in client_addr) {
  char response[MAX_BUF] = "👥 Online Users:\n";
  for (int i = 0; i < client_count; i++) {
    strcat(response, clients[i].nickname);
    strcat(response, "\n");
  }
  send_response(client_addr, response);
}

// 🔹 채팅 메시지 브로드캐스트 (같은 방의 모든 클라이언트에게 메시지 전송)
void broadcast_message(const char *room, const char *sender,
                       const char *message) {
  char buf[MAX_BUF];
  snprintf(buf, MAX_BUF, "[%s] %s", sender, message);

  // 채팅 내역을 파일에 저장하는 함수 호출
  save_chat_history(room, sender, message);

  for (int i = 0; i < client_count; i++) {
    if (strcmp(clients[i].room, room) == 0) {
      send_response(clients[i].addr, buf);
    }
  }
}

void save_chat_history(const char *room_name, const char *username,
                       const char *message) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s%s.txt", CHAT_HISTORY_DIR,
           room_name);  // 파일명: 채팅방이름.txt

  FILE *fp = fopen(filename, "a");  // 🔹 파일을 append 모드로 열기
  if (fp == NULL) {
    perror("채팅 기록 파일 열기 실패");
    return;
  }

  fprintf(fp, "[%s] %s\n", username,
          message);  // 🔹 "[유저명] 메시지" 형식으로 저장
  fclose(fp);
}

int is_id_exists(const char *id) {
  if (!conn) return 0;  // DB 연결 안 되어 있으면 ID 체크 불가

  char query[MAX_BUF];
  snprintf(query, sizeof(query),
           "SELECT user_id FROM chat_user WHERE user_id='%s';", id);

  if (mysql_query(conn, query) == 0) {
    MYSQL_RES *result = mysql_store_result(conn);
    int exists = (mysql_num_rows(result) > 0);  // 존재하면 1, 아니면 0
    mysql_free_result(result);
    return exists;
  }
  return 0;  // DB 오류 시 기본값 반환
}

int is_nickname_exists(const char *nickname) {
  if (!conn) return 0;  // DB 연결이 없으면 확인 불가

  char query[MAX_BUF];
  snprintf(query, sizeof(query), "SELECT name FROM chat_user WHERE name='%s';",
           nickname);

  if (mysql_query(conn, query) == 0) {
    MYSQL_RES *result = mysql_store_result(conn);
    int exists = (mysql_num_rows(result) > 0);  // 존재하면 1, 아니면 0
    mysql_free_result(result);
    return exists;
  }
  return 0;  // DB 오류 시 기본값 반환
}

// 🔹 클라이언트 요청 처리
void *receive_messages(void *arg) {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  char buf[MAX_BUF];

  while (1) {
    memset(buf, 0, MAX_BUF);
    int len = recvfrom(sfd, buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&client_addr, &client_len);
    if (len > 0) {
      buf[len] = '\0';

      char command[32] = {0}, arg1[64] = {0}, arg2[64] = {0},
           arg3[MAX_BUF] = {0};
      int scanned = sscanf(buf, "%s %s %s %[^\n]", command, arg1, arg2, arg3);

      // 클라이언트 닉네임 찾기
      char sender_nickname[32] = "Unknown";
      int client_index = -1;
      for (int i = 0; i < client_count; i++) {
        if (clients[i].active &&
            memcmp(&clients[i].addr, &client_addr, sizeof(client_addr)) == 0) {
          strcpy(sender_nickname, clients[i].nickname);
          client_index = i;
          break;
        }
      }

      printf("[SERVER] Received from %s: %s\n", sender_nickname, buf);

      if (strcmp(command, "FETCH_LOG") == 0) {
        send_chat_log(client_addr);  // 채팅 로그 전송 함수 호출
      } else if (strcmp(command, "PING") == 0) {
        if (client_index != -1) {
          clients[client_index].last_response = time(NULL);
          printf("[SERVER] PING received from %s, timestamp updated.\n",
                 sender_nickname);
        }
      } else if (strcmp(command, "LOGOUT") == 0) {
        if (client_index != -1) {
          printf("[SERVER] 클라이언트 %s 로그아웃\n", sender_nickname);
          clients[client_index].active = 0;
          memset(&clients[client_index], 0, sizeof(struct Client));
          client_count--;
        }
      } else if (strcmp(command, "REGISTER") == 0) {
        if (scanned >= 4) {
          register_user(arg1, arg2, arg3, client_addr);
        } else {
          send_response(client_addr, "등록 실패: 유효한 정보를 입력하세요.");
        }
      } else if (strcmp(command, "LOGIN") == 0) {
        if (scanned >= 3) {
          authenticate_user(arg1, arg2, client_addr);
        } else {
          send_response(client_addr, "로그인 실패: 유효한 정보를 입력하세요.");
        }
      } else if (strcmp(command, "CHECK_ID") == 0) {  // ID 중복 확인 추가
        if (is_id_exists(arg1)) {
          send_response(client_addr, "ID already exists");
        } else {
          send_response(client_addr, "ID available");
        }
      } else if (strcmp(command, "CHECK_NICKNAME") ==
                 0) {  // 닉네임 중복 확인 추가
        if (is_nickname_exists(arg1)) {
          send_response(client_addr, "Nickname already exists");
        } else {
          send_response(client_addr, "Nickname available");
        }
      } else if (strcmp(command, "LEAVE") == 0) {
        remove_client(client_addr);
      } else if (strcmp(command, "CREATE_ROOM") == 0) {
        if (scanned >= 2) {
          create_chat_room(arg1, sender_nickname, client_addr);
        } else {
          send_response(client_addr, "채팅방 생성 실패: 방 이름을 입력하세요.");
        }
      } else if (strcmp(command, "JOIN_ROOM") == 0) {
        if (scanned >= 2) {
          join_chat_room(arg1, client_addr);
        } else {
          send_response(client_addr, "채팅방 참가 실패: 방 이름을 입력하세요.");
        }
      } else if (strcmp(command, "LIST_ROOMS") == 0) {
        list_chat_rooms(client_addr);
      } else if (strcmp(command, "LIST_USERS") == 0) {
        list_users(client_addr);
      } else if (strcmp(command, "CHAT") == 0) {
        if (scanned >= 3 && strlen(arg3) > 0) {  // 빈 메시지 전송 방지
          broadcast_message(arg1, sender_nickname, arg3);
        } else {
          send_response(client_addr,
                        "채팅 실패: 방 이름과 메시지를 입력하세요.");
        }
      } else if (strcmp(command, "DELETE_ROOM") == 0) {
        if (scanned >= 2) {
          delete_chat_room(arg1, sender_nickname, client_addr);
        } else {
          send_response(client_addr, "채팅방 삭제 실패: 방 이름을 입력하세요.");
        }
      } else {
        printf("[SERVER] Unknown command received: %s\n", command);
      }
    }
  }
  return NULL;
}

void send_chat_log(struct sockaddr_in client_addr) {
  FILE *file = fopen("chat_log.txt", "r");
  if (file == NULL) {
    send_response(client_addr, "채팅 로그를 불러올 수 없습니다.");
    return;
  }

  char log_buffer[MAX_BUF] = {0};
  char line[MAX_BUF];

  // 로그 파일에서 데이터를 읽고 log_buffer에 저장
  while (fgets(line, sizeof(line), file) != NULL) {
    strcat(log_buffer, line);
    if (strlen(log_buffer) >= MAX_BUF - 100)  // 너무 길어지면 중단
      break;
  }
  fclose(file);

  send_response(client_addr, log_buffer);  // 클라이언트로 전송
}

int main() {
  pthread_t recv_thread;

  // 1. 데이터베이스 연결
  connect_db();

  // 2. 기존 채팅방 불러오기
  load_chat_rooms();

  // 3. UDP 소켓 생성
  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr_server = {.sin_family = AF_INET,
                                    .sin_port = htons(SERVER_PORT),
                                    .sin_addr.s_addr = htonl(INADDR_ANY)};

  // 4. 소켓 바인딩
  bind(sfd, (struct sockaddr *)&addr_server, sizeof(addr_server));

  printf("Server running...\n");

  // 5. 메시지 수신 스레드 시작
  pthread_create(&recv_thread, NULL, receive_messages, NULL);
  pthread_join(recv_thread, NULL);

  // 6. 서버 종료 시 MySQL 연결 종료
  if (conn) mysql_close(conn);
  close(sfd);

  return 0;
}