#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "define.h"

#define MAX_BUF 256
#define FBDEV "/dev/fb0"

#define COLOR_RED 0xff0000
#define COLOR_GREEN 0x00ff00
#define COLOR_BLUE 0x0000ff
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xffffff
#define COLOR_GRAY 0xaaaaaa
#define COLOR_LIGHTGRAY 0xdddddd

#define CHAT_WINDOW_X 250       // 오른쪽 큰 박스의 X 좌표
#define CHAT_WINDOW_Y 250       // 오른쪽 큰 박스의 Y 좌표
#define CHAT_WINDOW_WIDTH 300   // 오른쪽 큰 박스의 너비
#define CHAT_WINDOW_HEIGHT 200  // 오른쪽 큰 박스의 높이

// 나가기 (/quit) 버튼
#define BUTTON_SHARING_X_MIN 40
#define BUTTON_SHARING_X_MAX 240
#define BUTTON_SHARING_Y_MIN 280
#define BUTTON_SHARING_Y_MAX 330

// 로비로 전환환 (/exit) 버튼
#define BUTTON_EXIT_X_MIN 40
#define BUTTON_EXIT_X_MAX 240
#define BUTTON_EXIT_Y_MIN 340
#define BUTTON_EXIT_Y_MAX 390

// 파일 공유 (/share) 버튼
#define BUTTON_QUIT_X_MIN 40
#define BUTTON_QUIT_X_MAX 240
#define BUTTON_QUIT_Y_MIN 400
#define BUTTON_QUIT_Y_MAX 450

#define CHAT_AREA_X 10
#define CHAT_AREA_Y 50
#define CHAT_LINE_HEIGHT 20
#define MAX_MESSAGES 10

#define MAX_BUF 256

#define MAX_LINES 10        // 출력할 최대 줄 수
#define MAX_LINE_LENGTH 20  // 한 줄의 최대 길이

#define TOUCH_MAX_X 4096  // 터치 장치의 최대 X 값
#define TOUCH_MAX_Y 4096  // 터치 장치의 최대 Y 값
#define TOUCH_DEVICE "/dev/input/event1"

char messages[MAX_MESSAGES][1024];
int message_count = 0;

pid_t pid;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo *vip = &vinfo;
struct fb_fix_screeninfo *fip = &finfo;
char *map;

void Lcd_Put_Pixel_565(int xx, int yy, unsigned short value) {
  int location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
  *(unsigned short *)(map + location) = value;
}

void Lcd_Put_Pixel(int xx, int yy, int color) {
  int location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
  int r = color >> 16;
  int g = (color >> 8) & 0xff;
  int b = color & 0xff;
  *(unsigned short *)(map + location) =
      (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void draw_rect(int x, int y, int w, int h, unsigned int color) {
  int xx, yy;
  int location = 0;

  for (yy = y; yy < (y + h); yy++) {
    for (xx = x; xx < (x + w); xx++) {
      location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
      if (vip->bits_per_pixel == 32) { /* 32 bpp */
        *(unsigned int *)(map + location) = color;
      } else { /* 16 bpp */
        int r = color >> 16;
        int g = (color >> 8) & 0xff;
        int b = color & 0xff;
        *(unsigned short *)(map + location) =
            (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }
  }
}

////////////////////////
// bmp display
////////////////////////

#pragma pack(push, 1)

typedef struct {
  unsigned char magic[2];
  unsigned int size_of_file;
  unsigned char rsvd1[2];
  unsigned char rsvd2[2];
  unsigned int offset;
  unsigned int size_of_dib;
  unsigned int width;
  unsigned int height;
  unsigned short color_plane;
  unsigned short bpp;
  unsigned int compression;
  unsigned int size_of_image;
  unsigned int x_res;
  unsigned int y_res;
  unsigned int i_color;
} BMP_HDR;

#pragma pack(pop)

#define BMP_FILE ((BMP_HDR *)fp)

void Lcd_Print_BMP_Info(void *fp) {
  printf("MAGIC = %c%c\n", BMP_FILE->magic[0], BMP_FILE->magic[1]);
  printf("BMP SIZE = %d\n", BMP_FILE->size_of_file);
  printf("RAW OFFSET = %d\n", BMP_FILE->offset);
  printf("DIB SIZE = %d\n", BMP_FILE->size_of_dib);
  printf("WIDTH = %d, HEIGHT = %d\n", BMP_FILE->width, BMP_FILE->height);
  printf("BPP = %d\n", BMP_FILE->bpp);
}

typedef struct {
  unsigned char blue;
  unsigned char green;
  unsigned char red;
} PIXEL;

#define PIX565(p, x)                                           \
  ((((p)[x].red & 0xF8) << 8) | (((p)[x].green & 0xFC) << 3) | \
   (((p)[x].blue) >> 3))

void Lcd_Draw_BMP_File_24bpp(int x, int y, void *fp) {
  int xx, yy;
  unsigned int pad = (4 - ((BMP_FILE->width * 3) % 4)) % 4;
  unsigned char *pix = (unsigned char *)fp + BMP_FILE->offset;

  for (yy = (BMP_FILE->height - 1) + y; yy >= y; yy--) {
    for (xx = x; xx < BMP_FILE->width + x; xx++) {
      Lcd_Put_Pixel_565(xx, yy, PIX565((PIXEL *)pix, xx - x));
    }

    pix = pix + (BMP_FILE->width * sizeof(PIXEL)) + pad;
  }
}

void draw_bmp(const char *fname, int x, int y) {
  struct stat statb;
  int fd, ret;
  int msize;

  if (stat(fname, &statb) == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return;
  }

  msize = statb.st_size + 1024;
  char *p = malloc(msize);
  if (p == NULL) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return;
  }

  fd = open(fname, O_RDONLY);
  if (fd == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return;
  }
  ret = read(fd, p, msize);
  if (ret == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return;
  }
  Lcd_Print_BMP_Info(p);
  Lcd_Draw_BMP_File_24bpp(x, y, p);
}

////////////////////////
// font display
////////////////////////

#include <stdarg.h>
#include <string.h>
void Lcd_Put_Pixel(int x, int y, int color);

static unsigned char _first[] = {0,  0,  1,  2,  3,  4,  5,  6,  7,  8, 9,
                                 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
static unsigned char _middle[] = {0,  0,  0,  1,  2,  3,  4,  5,  0,  0,
                                  6,  7,  8,  9,  10, 11, 0,  0,  12, 13,
                                  14, 15, 16, 17, 0,  0,  18, 19, 20, 21};
static unsigned char _last[] = {0,  0,  1,  2,  3,  4,  5,  6,  7,  8,
                                9,  10, 11, 12, 13, 14, 15, 16, 0,  17,
                                18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
static unsigned char cho[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3,
                              3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0};
static unsigned char cho2[] = {0, 5, 5, 5, 5, 5, 5, 5, 5, 6, 7,
                               7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5};
static unsigned char jong[] = {0, 0, 2, 0, 2, 1, 2, 1, 2, 3, 0,
                               2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1};

#include "Fonts/ENG8X16.H"
#include "Fonts/HAN16X16.H"
#include "Fonts/HANTABLE.H"

#define ENG_FONT_X 8
#define ENG_FONT_Y 16

#define COPY(A, B) \
  for (loop = 0; loop < 32; loop++) *(B + loop) = *(A + loop);
#define OR(A, B) \
  for (loop = 0; loop < 32; loop++) *(B + loop) |= *(A + loop);

void Lcd_Han_Putch(int x, int y, int color, int bkcolor, int data, int zx,
                   int zy) {
  unsigned int first, middle, last;
  unsigned int offset, loop;
  unsigned char xs, ys;
  unsigned char temp[32];
  unsigned char bitmask[] = {128, 64, 32, 16, 8, 4, 2, 1};

  first = (unsigned)((data >> 8) & 0x00ff);
  middle = (unsigned)(data & 0x00ff);
  offset = (first - 0xA1) * (0x5E) + (middle - 0xA1);
  first = *(HanTable + offset * 2);
  middle = *(HanTable + offset * 2 + 1);
  data = (int)((first << 8) + middle);

  first = _first[(data >> 10) & 31];
  middle = _middle[(data >> 5) & 31];
  last = _last[(data) & 31];

  if (last == 0) {
    offset = (unsigned)(cho[middle] * 640);
    offset += first * 32;
    COPY(han16x16 + offset, temp);

    if (first == 1 || first == 24)
      offset = 5120;
    else
      offset = 5120 + 704;
    offset += middle * 32;
    OR(han16x16 + offset, temp);
  } else {
    offset = (unsigned)(cho2[middle] * 640);
    offset += first * 32;
    COPY(han16x16 + offset, temp);

    if (first == 1 || first == 24)
      offset = 5120 + 704 * 2;
    else
      offset = 5120 + 704 * 3;
    offset += middle * 32;
    OR(han16x16 + offset, temp);

    offset = (unsigned)(5120 + 2816 + jong[middle] * 896);
    offset += last * 32;
    OR(han16x16 + offset, temp);
  }

  for (ys = 0; ys < 16; ys++) {
    for (xs = 0; xs < 8; xs++) {
      if (temp[ys * 2] & bitmask[xs]) {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs, y + ys, color);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * xs, y + ys, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, color);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + xs, y + 2 * ys, color);
          Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, color);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, color);
        }
      } else {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs, y + ys, bkcolor);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * xs, y + ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, bkcolor);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + xs, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, bkcolor);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, bkcolor);
        }
      }
    }

    for (xs = 0; xs < 8; xs++) {
      if (temp[ys * 2 + 1] & bitmask[xs]) {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs + 8, y + ys, color);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + ys, color);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + ys, color);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys, color);
          Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys + 1, color);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys + 1, color);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys + 1, color);
        }
      } else {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs + 8, y + ys, bkcolor);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + ys, bkcolor);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys + 1, bkcolor);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys + 1, bkcolor);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys + 1, bkcolor);
        }
      }
    }
  }
}

void Lcd_Eng_Putch(int x, int y, int color, int bkcolor, int data, int zx,
                   int zy) {
  unsigned offset, loop;
  unsigned char xs, ys;
  unsigned char temp[32];
  unsigned char bitmask[] = {128, 64, 32, 16, 8, 4, 2, 1};

  offset = (unsigned)(data * 16);
  COPY(eng8x16 + offset, temp);

  for (ys = 0; ys < 16; ys++) {
    for (xs = 0; xs < 8; xs++) {
      if (temp[ys] & bitmask[xs]) {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs, y + ys, color);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * xs, y + ys, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, color);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + xs, y + 2 * ys, color);
          Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, color);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, color);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, color);
        }
      } else {
        if ((zx == 1) && (zy == 1))
          Lcd_Put_Pixel(x + xs, y + ys, bkcolor);
        else if ((zx == 2) && (zy == 1)) {
          Lcd_Put_Pixel(x + 2 * xs, y + ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, bkcolor);
        } else if ((zx == 1) && (zy == 2)) {
          Lcd_Put_Pixel(x + xs, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, bkcolor);
        } else if ((zx == 2) && (zy == 2)) {
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, bkcolor);
          Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, bkcolor);
        }
      }
    }
  }
}

void Lcd_Puts(int x, int y, int color, int bkcolor, char *str, int zx, int zy) {
  unsigned data;

  while (*str) {
    data = *str++;
    if (data >= 128) {
      data *= 256;
      data |= *str++;
      Lcd_Han_Putch(x, y, color, bkcolor, (int)data, zx, zy);
      x += zx * 16;
    } else {
      Lcd_Eng_Putch(x, y, color, bkcolor, (int)data, zx, zy);
      x += zx * ENG_FONT_X;
    }
  }
}

void Lcd_Printf(int x, int y, int color, int bkcolor, int zx, int zy, char *fmt,
                ...) {
  va_list ap;
  char string[256];

  va_start(ap, fmt);
  vsprintf(string, fmt, ap);
  Lcd_Puts(x, y, color, bkcolor, string, zx, zy);
  va_end(ap);
}

void Lcd_Clear_Screen(void) {
  draw_rect(0, 0, vinfo.xres, vinfo.yres, COLOR_WHITE);
}

#define MY_STR_CNT_MAX 10
#define MY_STR_LEN_MAX 256

#define MY_STR_MSG_MENU 0
#define MY_STR_MSG_LEVEL 1
#define MY_STR_MSG_START 2
#define MY_STR_MSG_SCORE 3
#define MY_STR_MSG_END 4

char my_str[MY_STR_CNT_MAX][MY_STR_LEN_MAX];
int my_str_cnt;

void Init_String(void) {
  FILE *fp = fopen("string.txt", "r");
  int i, j, len;
  if (fp == NULL) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return;
  }
  for (i = 0; i < MY_STR_CNT_MAX; i++) {
    if (fgets(my_str[i], MY_STR_LEN_MAX, fp) == NULL) {
      break;
    }
    // delete \r or \n
    len = strlen(my_str[i]);
    for (j = len - 1; j >= 0; j--) {
      if (my_str[i][j] == 0x0d || my_str[i][j] == 0x0a) {
        my_str[i][j] = 0;
      } else {
        break;
      }
    }
#if 0
      for(j=0; j<len; j++) {
         printf("%02x ", my_str[i][j]);
      }
      printf("\n");
#endif
    my_str_cnt++;
  }
}

void Font_Test(void) {
  printf("\nFont Display\n");
  Lcd_Clear_Screen();
  Lcd_Printf(2, 2, COLOR_BLACK, COLOR_RED, 2, 2, "1234ABCD%^&*");
  Lcd_Printf(2, 100, COLOR_WHITE, COLOR_BLACK, 2, 2, "%s",
             my_str[MY_STR_MSG_MENU]);
  Lcd_Printf(2, 200, COLOR_BLUE, COLOR_RED, 2, 2, "%s 98",
             my_str[MY_STR_MSG_SCORE]);
  Lcd_Printf(2, 300, COLOR_BLACK, COLOR_WHITE, 1, 1, "%s",
             my_str[MY_STR_MSG_END]);
}

////////////////////////

void draw_chat_messages() {
  int i;

  // 채팅창을 지우기 위해 배경색으로 다시 그리기
  draw_rect(CHAT_WINDOW_X, CHAT_WINDOW_Y, CHAT_WINDOW_WIDTH, CHAT_WINDOW_HEIGHT,
            COLOR_LIGHTGRAY);

  for (i = 0; i < message_count; i++) {
    int x = CHAT_WINDOW_X + 10;  // 텍스트 시작 X 좌표 (여백 추가)
    int y = CHAT_WINDOW_Y + i * CHAT_LINE_HEIGHT;

    // 채팅 텍스트가 Chatting window의 높이를 벗어나면 출력 중단
    if (y + CHAT_LINE_HEIGHT > CHAT_WINDOW_Y + CHAT_WINDOW_HEIGHT) {
      break;
    }

    // 메시지 줄바꿈 처리
    char *line = messages[i];
    int line_length = strlen(line);
    int max_chars_per_line =
        (CHAT_WINDOW_WIDTH - 20) /
        ENG_FONT_X;  // 한 줄에 들어갈 최대 문자 수 (여백 고려)

    while (line_length > 0) {
      char buffer[1024] = {0};
      int chars_to_print =
          (line_length > max_chars_per_line) ? max_chars_per_line : line_length;

      strncpy(buffer, line, chars_to_print);
      buffer[chars_to_print] = '\0';

      // 텍스트 출력
      Lcd_Printf(x, y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1, "%s", messages[i]);

      // 다음 줄로 이동
      y += CHAT_LINE_HEIGHT;
      if (y + CHAT_LINE_HEIGHT > CHAT_WINDOW_Y + CHAT_WINDOW_HEIGHT) {
        break;  // 창의 높이를 초과하면 출력 중단
      }

      line += chars_to_print;
      line_length -= chars_to_print;
    }
  }
}

void add_message(const char *message) {
  if (message_count < MAX_MESSAGES) {
    strcpy(messages[message_count++], message);
  } else {
    // 메시지가 가득 찼을 경우, 위로 밀기
    memmove(messages, messages + 1, (MAX_MESSAGES - 1) * sizeof(messages[0]));
    strcpy(messages[MAX_MESSAGES - 1], message);
  }
  draw_chat_messages();
}

#if 1

void Draw_Chat_UI(void) {
  int screen_width = vinfo.xres;   // 화면 너비
  int screen_height = vinfo.yres;  // 화면 높이

  // 전체 UI 크기 설정
  int margin = 20;          // 외부 여백
  int sidebar_width = 200;  // 왼쪽 사이드바 너비
  int button_height = 50;   // 버튼 높이
  int button_spacing = 10;  // 버튼 간 간격
  int client_list_height =
      5 * button_height;      // Client List 박스 높이 (버튼 크기의 5배)
  int chat_area_margin = 20;  // 채팅 영역 내부 여백
  int chat_width = screen_width - sidebar_width - 3 * margin;  // 채팅 영역 너비
  int chat_height = screen_height - 2 * margin;                // 채팅 영역 높이

  // 화면 초기화
  Lcd_Clear_Screen();

  // Client List 박스 (사이드바 상단에 독립된 큰 박스)
  draw_rect(margin, margin, sidebar_width, client_list_height,
            COLOR_LIGHTGRAY);  // Client List 박스 배경색 연한 회색
  Lcd_Printf(margin + 20, margin + 20, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1,
             "Client List");

  // 버튼들 (Virtual Sharing, Screen Sharing, Quit - 회색)
  draw_rect(margin + 20, margin + client_list_height + button_spacing,
            sidebar_width - 40, button_height, COLOR_GRAY);
  Lcd_Printf(margin + 40, margin + client_list_height + button_spacing + 15,
             COLOR_BLACK, COLOR_GRAY, 1, 1, "Virtual Sharing");

  draw_rect(margin + 20,
            margin + client_list_height + 2 * button_spacing + button_height,
            sidebar_width - 40, button_height, COLOR_GRAY);
  Lcd_Printf(
      margin + 40,
      margin + client_list_height + 2 * button_spacing + button_height + 15,
      COLOR_BLACK, COLOR_GRAY, 1, 1, "Screen Sharing");

  draw_rect(
      margin + 20,
      margin + client_list_height + 3 * button_spacing + 2 * button_height,
      sidebar_width - 40, button_height, COLOR_GRAY);
  Lcd_Printf(
      margin + 40,
      margin + client_list_height + 3 * button_spacing + 2 * button_height + 15,
      COLOR_BLACK, COLOR_GRAY, 1, 1, "Quit");

  // 채팅 영역 (오른쪽 큰 박스 - 텍스트 중앙 배치 및 배경색 연한 회색)
  draw_rect(2 * margin + sidebar_width, margin, chat_width, chat_height,
            COLOR_LIGHTGRAY);  // 채팅 영역 배경색 연한 회색

  // 채팅 윈도우 배경 (텍스트 바탕색과 동일)
  draw_rect(CHAT_WINDOW_X, CHAT_WINDOW_Y, CHAT_WINDOW_WIDTH, CHAT_WINDOW_HEIGHT,
            COLOR_BLACK);

  // 채팅 윈도우 테두리
  draw_rect(CHAT_WINDOW_X - 1, CHAT_WINDOW_Y - 1, CHAT_WINDOW_WIDTH + 2,
            CHAT_WINDOW_HEIGHT + 2, COLOR_LIGHTGRAY);

  // 채팅 로그 파일 읽기 및 출력
  FILE *log_file = fopen("chat_log.txt", "r");  // chat_log.txt 파일 열기
  if (log_file == NULL) {
    Lcd_Printf(CHAT_WINDOW_X + 10, CHAT_WINDOW_Y + 10, COLOR_RED,
               COLOR_LIGHTGRAY, 1, 1, "Error: Unable to open chat_log.txt");
    return;
  }

  char line[MAX_LINE_LENGTH];
  int line_y = CHAT_WINDOW_Y + 10;  // 첫 번째 줄 출력 위치
  int line_count = 0;

  // chat_log.txt에서 한 줄씩 읽어와 출력
  while (fgets(line, sizeof(line), log_file) != NULL &&
         line_count < MAX_LINES) {
    // 개행 문자 제거
    line[strcspn(line, "\n")] = '\0';

    // 화면에 출력
    Lcd_Printf(CHAT_WINDOW_X + 10, line_y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1,
               line);

    // 다음 줄로 이동
    line_y += 20;  // 한 줄당 20픽셀 간격
    line_count++;
  }

  fclose(log_file);  // 파일 닫기

  // 채팅 영역 텍스트 중앙 배치 (기본 메시지)
  if (line_count == 0) {
    int chat_text_x =
        2 * margin + sidebar_width + (chat_width / 2) -
        (strlen("No messages yet") * 8 / 2);  // 문자열 길이에 따라 중앙 정렬
    int chat_text_y =
        margin + (chat_height / 2) - 8;  // 텍스트 높이를 고려한 중앙 정렬
    Lcd_Printf(chat_text_x, chat_text_y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1,
               "No messages yet");
  }
}

#endif

void Update_Chat_Window() {
  int screen_width = vinfo.xres;   // 화면 너비
  int screen_height = vinfo.yres;  // 화면 높이

  // 전체 UI 크기 설정
  int margin = 20;          // 외부 여백
  int sidebar_width = 200;  // 왼쪽 사이드바 너비
  int button_height = 50;   // 버튼 높이
  int button_spacing = 10;  // 버튼 간 간격
  int client_list_height =
      5 * button_height;      // Client List 박스 높이 (버튼 크기의 5배)
  int chat_area_margin = 20;  // 채팅 영역 내부 여백
  int chat_width = screen_width - sidebar_width - 3 * margin;  // 채팅 영역 너비
  int chat_height = screen_height - 2 * margin;                // 채팅 영역 높이

  // 채팅 윈도우 배경 (텍스트 바탕색과 동일)
  draw_rect(CHAT_WINDOW_X, CHAT_WINDOW_Y, CHAT_WINDOW_WIDTH, CHAT_WINDOW_HEIGHT,
            COLOR_BLACK);

  // 채팅 윈도우 테두리
  draw_rect(CHAT_WINDOW_X - 1, CHAT_WINDOW_Y - 1, CHAT_WINDOW_WIDTH + 2,
            CHAT_WINDOW_HEIGHT + 2, COLOR_LIGHTGRAY);

  // 채팅 로그 파일 읽기 및 출력
  FILE *log_file = fopen("chat_log.txt", "r");  // chat_log.txt 파일 열기
  if (log_file == NULL) {
    Lcd_Printf(CHAT_WINDOW_X + 10, CHAT_WINDOW_Y + 10, COLOR_RED,
               COLOR_LIGHTGRAY, 1, 1, "Error: Unable to open chat_log.txt");
    return;
  }

  char line[MAX_LINE_LENGTH];
  int line_y = CHAT_WINDOW_Y + 10;  // 첫 번째 줄 출력 위치
  int line_count = 0;

  // chat_log.txt에서 한 줄씩 읽어와 출력
  while (fgets(line, sizeof(line), log_file) != NULL &&
         line_count < MAX_LINES) {
    // 개행 문자 제거
    line[strcspn(line, "\n")] = '\0';

    // 화면에 출력
    Lcd_Printf(CHAT_WINDOW_X + 10, line_y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1,
               line);

    // 다음 줄로 이동
    line_y += 20;  // 한 줄당 20픽셀 간격
    line_count++;
  }

  fclose(log_file);  // 파일 닫기

  // 채팅 영역 텍스트 중앙 배치 (기본 메시지)
  if (line_count == 0) {
    int chat_text_x =
        2 * margin + sidebar_width + (chat_width / 2) -
        (strlen("No messages yet") * 8 / 2);  // 문자열 길이에 따라 중앙 정렬
    int chat_text_y =
        margin + (chat_height / 2) - 8;  // 텍스트 높이를 고려한 중앙 정렬
    Lcd_Printf(chat_text_x, chat_text_y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1,
               "No messages yet");
  }
}

void send_request(const char *request) {
  char buf[MAX_BUF];
  snprintf(buf, MAX_BUF, "%s", request);

  int len = sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
                   addr_server_len);
  if (len < 0) {
    perror("서버 요청 전송 실패");
  }

  receive_response();  // 서버 응답 대기
}

void receive_response() {
  char buf[MAX_BUF];
  memset(buf, 0, MAX_BUF);

  int len = recvfrom(sfd, buf, MAX_BUF - 1, 0, (struct sockaddr *)&addr_server,
                     &addr_server_len);
  if (len < 0) {
    perror("서버 응답 수신 실패");
    return;
  }

  buf[len] = '\0';
  printf("\n[Server Response] %s\n", buf);
}

void fetch_chat_log() {
  char buf[MAX_BUF];

  printf("서버로부터 채팅 로그 요청 중...\n");

  // 서버에 채팅 로그 요청
  send_request("FETCH_LOG");

  memset(buf, 0, MAX_BUF);
  int len = recvfrom(sfd, buf, MAX_BUF - 1, 0, (struct sockaddr *)&addr_server, &addr_server_len);
}

//////////////////////////
// UDP 통신 파트
///////////////////////
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
  printf("\n [CLIENT] Logging out...\n");
  snprintf(buf, MAX_BUF, "LOGOUT %s", nickname);
  sendto(sfd, buf, strlen(buf), 0, (struct sockaddr *)&addr_server,
         sizeof(addr_server));
  close(sfd);
  exit(0);
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

void chatroom_command(char *command) {
  if (strcmp(command, "/share") == 0) {
    printf("파일 공유 기능 실행 중...\n");
    send_request("SHARE_FILE");
  } else if (strcmp(command, "/exit") == 0) {
    printf("채팅방을 나갑니다...\n");
    send_request("EXIT_ROOM");
    if (access("./lobbypage", X_OK) == 0) {
      execl("./lobbypage", "./lobbypage", NULL);
      perror("로비 페이지 실행 실패");
    } else {
      printf("실행 파일이 존재하지 않거나 실행 권한이 없습니다\n");
    }
  } else if (strcmp(command, "/quit") == 0) {
    printf("클라이언트를 종료합니다...\n");
    exit_client();
  } else {
    printf("잘못된 명령어입니다. 다시 입력하세요.\n");
  }
}

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// 터치 장치 경로
char touch_device[64] = TOUCH_DEVICE;

// 터치 좌표 저장
int touch_x = -1, touch_y = -1;

// 터치 입력 대기
void wait_for_touch() {
  int fd;
  struct input_event ev;
  int last_x = -1, last_y = -1;
  int touch_detected = 0;

  fd = open(TOUCH_DEVICE, O_RDONLY);
  if (fd < 0) {
    perror("❌ 터치 장치 열기 실패! 경로 확인 필요");
    exit(EXIT_FAILURE);
  }

  touch_x = -1;
  touch_y = -1;

  while (1) {
    if (read(fd, &ev, sizeof(struct input_event)) < 0) {
      perror("❌ 터치 입력 읽기 실패, 재시도 중...");
      continue;
    }

    // ✅ X 좌표 저장
    if (ev.type == EV_ABS && ev.code == ABS_X) {
      last_x = (ev.value * SCREEN_WIDTH) / TOUCH_MAX_X;
    }
    // ✅ Y 좌표 저장
    else if (ev.type == EV_ABS && ev.code == ABS_Y) {
      last_y = (ev.value * SCREEN_HEIGHT) / TOUCH_MAX_Y;
    }

    // ✅ 터치 시작 감지
    if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 1) {
      touch_detected = 1;
    }
    // ✅ 터치 끝났을 때 최종 좌표 저장 (출력 없음)
    else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0 &&
             touch_detected) {
      touch_x = last_x;
      touch_y = last_y;
      break;  // 터치가 끝나면 while 루프 종료
    }

    if (ev.type == EV_ABS) {
      if (ev.code == ABS_X) {
        touch_x = (ev.value * SCREEN_WIDTH) / 3200;
      } else if (ev.code == ABS_Y) {
        touch_y = (ev.value * SCREEN_HEIGHT) / 2400;
      }
    }

    if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
      if (ev.value == 1) {
        touch_detected = 1;
      } else if (ev.value == 0 && touch_detected) {
        break;
      }
    }
  }

  close(fd);

  // ✅ 좌표 값이 유효하지 않으면 초기화
  if (touch_x < 0 || touch_y < 0) {
    touch_x = 0;
    touch_y = 0;
  }
}

void chatroom_screen() {
  printf("\n=== 채팅방 ===\n");
  printf("[ 공유 ]\n");
  printf("[ 나가기 ]\n");
  printf("[ 종료 ]\n");

  fetch_chat_log();  // ✅ 서버에서 채팅 로그를 가져와서 업데이트

  while (1) {
    wait_for_touch();
    printf("터치 감지 X = %d, Y = %d\n", touch_x, touch_y);

    if (touch_x >= BUTTON_SHARING_X_MIN && touch_x <= BUTTON_SHARING_X_MAX &&
        touch_y >= BUTTON_SHARING_Y_MIN && touch_y <= BUTTON_SHARING_Y_MAX) {
      chatroom_command("/share");
    }

    else if (touch_x >= BUTTON_EXIT_X_MIN && touch_x <= BUTTON_EXIT_X_MAX &&
             touch_y >= BUTTON_EXIT_Y_MIN && touch_y <= BUTTON_EXIT_Y_MAX) {
      chatroom_command("/exit");
      break;
    }

    else if (touch_x >= BUTTON_QUIT_X_MIN && touch_x <= BUTTON_QUIT_X_MAX &&
             touch_y >= BUTTON_QUIT_Y_MIN && touch_y <= BUTTON_QUIT_Y_MAX) {
      chatroom_command("/quit");
    }
  }
}

int main(int argc, char **argv) {
  int fd;
  int size;

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

  // 종료 시 "LOGOUT" 메시지를 보내도록 신호 핸들러 설정
  signal(SIGINT, handle_exit);

  // PING 메시지를 보내는 스레드 시작
  pthread_t ping_thread;
  pthread_create(&ping_thread, NULL, (void *)send_ping, NULL);

  system("clear");  // 화면 클리어하여 로그인 화면 잔상 제거

  if (argc > 1) {
    strncpy(nickname, argv[1], sizeof(nickname) - 1);
    nickname[sizeof(nickname) - 1] = '\0';
    printf("\n로그인 성공! 환영합니다, %s 님!\n", nickname);
  } else {
    printf("\n로그인 성공! (ID 없음)\n");
  }

  if (argc != 1) {
    printf("usage: %s\n", argv[0]);
    return EXIT_FAILURE;
  }
  printf("[%d] running %s\n", pid = getpid(), argv[0]);

  /* open */
  fd = open(FBDEV, O_RDWR);
  if (fd == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] %s was opened successfully\n", pid, FBDEV);

  /* get fb_var_screeninfo */
  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] %dx%d %dbpp\n", pid, vinfo.xres, vinfo.yres,
         vinfo.bits_per_pixel);

  /* get fb_fix_screeninfo */
  if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] line_length is %d\n", pid, finfo.line_length);

  /* mmap */
  size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
  map = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == (char *)-1) {
    printf("[%d] error: %s (%d)\n", pid, strerror(errno), __LINE__);
    return EXIT_FAILURE;
  }
  printf("[%d] %s was mapped to %p\n", pid, FBDEV, map);

  /* UI 그리기 */
  Draw_Chat_UI();

  /* close */
  munmap(map, size);
  close(fd);

  chatroom_screen();

  while (1) {
    char command[32];
    printf("\n 채팅방 명령 입력 (/share, /exit, /quit): ");
    scanf("%s", command);
    getchar();  // 개행 문자 제거

    chatroom_command(command);  // 로비 명령어 처리
  }

  close(sfd);
  return EXIT_SUCCESS;
}