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

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480
#define TOUCH_DEVICE "/dev/input/event1"
#define ALLOWED_SERVER_IP "10.10.141.13"

#define BUTTON_DELETE_X_MIN 670
#define BUTTON_DELETE_X_MAX 790
#define BUTTON_DELETE_Y_MIN 340
#define BUTTON_DELETE_Y_MAX 390

#define BUTTON_QUIT_X_MIN 670
#define BUTTON_QUIT_X_MAX 790
#define BUTTON_QUIT_Y_MIN 420
#define BUTTON_QUIT_Y_MAX 470

#define INPUT_ID_X_MIN 300
#define INPUT_ID_X_MAX 550
#define INPUT_ID_Y_MIN 200
#define INPUT_ID_Y_MAX 250

#define INPUT_PW_X_MIN 300
#define INPUT_PW_X_MAX 550
#define INPUT_PW_Y_MIN 260
#define INPUT_PW_Y_MAX 310

typedef enum { SCREEN_LOGIN, SCREEN_ROOM_SELECTION } ScreenState;

ScreenState current_screen = SCREEN_LOGIN;
int touch_active = 0;
int input_field_selected = 0;  // 0: None, 1: ID, 2: PW
char input_id[32] = "";
char input_pw[32] = "";

#define TOUCH_MAX_X 4096  // 터치 장치의 최대 X 값
#define TOUCH_MAX_Y 4096  // 터치 장치의 최대 Y 값

// 버튼 좌표 정의
#define BUTTON_JOIN_X_MIN 515
#define BUTTON_JOIN_X_MAX 665
#define BUTTON_JOIN_Y_MIN 355
#define BUTTON_JOIN_Y_MAX 405

#define BUTTON_CREATE_X_MIN 515
#define BUTTON_CREATE_X_MAX 665
#define BUTTON_CREATE_Y_MIN 435
#define BUTTON_CREATE_Y_MAX 485

pid_t pid;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo *vip = &vinfo;
struct fb_fix_screeninfo *fip = &finfo;
char *map;

/////////////////////////
// UI 그리기 시작
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

void Draw_New_Form_UI(void) {
  int center_x = vinfo.xres / 2;  // 화면 중심 X 좌표
  int center_y = vinfo.yres / 2;  // 화면 중심 Y 좌표
  int y_offset = 30;              // 위로 이동할 총 오프셋

  // 화면 초기화
  Lcd_Clear_Screen();

  // 박스 및 텍스트 크기 설정
  int user_name_width = 200;     // 사용자 이름 필드 너비
  int user_name_height = 40;     // 사용자 이름 필드 높이
  int room_list_width = 400;     // 방 목록 박스 너비
  int room_list_height = 200;    // 방 목록 박스 높이
  int client_list_width = 300;   // 클라이언트 목록 박스 너비
  int client_list_height = 100;  // 클라이언트 목록 박스 높이
  int button_width = 150;        // 버튼 너비
  int button_height = 50;        // 버튼 높이
  int spacing = 30;              // 요소 간 간격
  int offset_x = 50;             // 좌우 이동 거리

  // [User Name] 필드
  Lcd_Printf(center_x - room_list_width / 2,
             center_y - room_list_height / 2 - user_name_height - 2 * spacing -
                 y_offset,
             COLOR_BLACK, COLOR_WHITE, 2, 2, "[User Name]");

  // 대충 방 목록 박스
  draw_rect(center_x - room_list_width / 2,
            center_y - room_list_height / 2 - y_offset, room_list_width,
            room_list_height, COLOR_BLACK);
  Lcd_Printf(
      center_x - room_list_width / 2 + room_list_width / 2 - 70,
      center_y - room_list_height / 2 + room_list_height / 2 - 10 - y_offset,
      COLOR_WHITE, COLOR_BLACK, 2, 2, "Room List");

  // 대충 접속 중인 클라이언트 목록 박스 (왼쪽으로 이동)
  draw_rect(center_x - room_list_width / 2 - offset_x,
            center_y + room_list_height / 2 + spacing - y_offset,
            client_list_width, client_list_height, COLOR_BLACK);
  Lcd_Printf(
      center_x - room_list_width / 2 - offset_x + client_list_width / 2 - 70,
      center_y + room_list_height / 2 + spacing + client_list_height / 2 - 10 -
          y_offset,
      COLOR_WHITE, COLOR_BLACK, 2, 2, "Client List");

  // Join the Room 버튼 (오른쪽으로 이동)
  draw_rect(center_x + room_list_width / 2 + offset_x - button_width,
            center_y + room_list_height / 2 + spacing - y_offset, button_width,
            button_height, COLOR_LIGHTGRAY);
  Lcd_Printf(center_x + room_list_width / 2 + offset_x - button_width / 2 - 25,
             center_y + room_list_height / 2 + spacing + button_height / 2 -
                 10 - y_offset,
             COLOR_BLACK, COLOR_LIGHTGRAY, 2, 2, "Join");

  // Create the Room 버튼 (오른쪽으로 이동, 텍스트 중앙 정렬)
  draw_rect(center_x + room_list_width / 2 + offset_x - button_width,
            center_y + room_list_height / 2 + spacing + button_height +
                spacing - y_offset,
            button_width, button_height, COLOR_LIGHTGRAY);
  Lcd_Printf(center_x + room_list_width / 2 + offset_x - button_width / 2 - 50,
             center_y + room_list_height / 2 + spacing + button_height +
                 spacing + button_height / 2 - 8 - y_offset,
             COLOR_BLACK, COLOR_LIGHTGRAY, 2, 2, "Create");

  draw_rect(670, 340, 120, 50, COLOR_LIGHTGRAY);
  Lcd_Printf(690, 355, COLOR_BLACK, COLOR_LIGHTGRAY, 2, 2, "Delete");

  // Create 버튼 옆 QUIT 버튼 추가
  draw_rect(670, 420, 120, 50, COLOR_LIGHTGRAY);
  Lcd_Printf(690, 435, COLOR_BLACK, COLOR_LIGHTGRAY, 2, 2, "Quit");
}

///////////////////////////////////

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
    perror("recvfrom failed");  // recvfrom() 에러 로그 추가
    printf("No response from server. Connection issue or timeout.\n");
    return 0;
  }

  buf[len] = '\0';
  printf("\n[Server] %s\n", buf);
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

// 로비 명령어
void lobby_command(char *command) {
  char room_name[32];

  if (strcmp(command, "/Join") == 0) {
    printf("참가할 채팅방 이름 입력: ");
    scanf("%s", room_name);
    getchar();
    snprintf(buf, MAX_BUF, "JOIN_ROOM %s", room_name);
    send_request(buf);

    if (strstr(buf, "채팅방 참가 성공")) {
      printf("채팅방 입장: %s\n", room_name);
    } else {
      printf("채팅방 참가 실패\n");
    }
  } else if (strcmp(command, "/Create") == 0) {
    printf("새로운 채팅방 이름 입력: ");
    scanf("%s", room_name);
    getchar();
    snprintf(buf, MAX_BUF, "CREATE_ROOM %s", room_name);
    send_request(buf);

    if (strstr(buf, "채팅방이 성공적으로 생성")) {
      printf("방 생성 성공: %s\n", room_name);
    } else {
      printf("방 생성 실패\n");
    }
  } else if (strcmp(command, "/Delete") == 0) {
    printf("삭제할 채팅방 이름 입력: ");
    scanf("%s", room_name);
    getchar();
    snprintf(buf, MAX_BUF, "DELETE_ROOM %s", room_name);
    send_request(buf);

    if (strstr(buf, "채팅방이 성공적으로 삭제")) {
      printf("방 삭제 성공: %s\n", room_name);
    } else {
      printf("방 삭제 실패\n");
    }
  } else if (strcmp(command, "/Quit") == 0) {
    printf("클라이언트를 종료합니다\n");
    exit_client();
  } else {
    printf("잘못된 명령어입니다 다시 입력하세요\n");
  }
}

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
      perror("터치 입력 읽기 실패, 재시도 중...");
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
      touch_x = ((last_x - 45) * 800) / (755 - 45);
      touch_y = ((last_y - 35) * 480) / (455 - 35);
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

void lobby_screen() {
  printf("\n=== 🖥️ Online A/S Center ===\n");
  printf("[ Join the Room ]\n");
  printf("[ Create the Room ]\n");

  while (1) {
    wait_for_touch();
    printf("터치 감지: X = %d, Y = %d\n", touch_x, touch_y);

    // Join the Room 버튼 터치 시
    if (touch_x >= BUTTON_JOIN_X_MIN && touch_x <= BUTTON_JOIN_X_MAX &&
        touch_y >= BUTTON_JOIN_Y_MIN && touch_y <= BUTTON_JOIN_Y_MAX) {
      lobby_command("/Join");
      continue;
    }

    // Create the Room 버튼 터치 시
    else if (touch_x >= BUTTON_CREATE_X_MIN && touch_x <= BUTTON_CREATE_X_MAX &&
             touch_y >= BUTTON_CREATE_Y_MIN && touch_y <= BUTTON_CREATE_Y_MAX) {
      lobby_command("/Create");
      continue;
    }

    else if (touch_x >= BUTTON_DELETE_X_MIN && touch_x <= BUTTON_DELETE_X_MAX &&
             touch_y >= BUTTON_DELETE_Y_MIN && touch_y <= BUTTON_DELETE_Y_MAX) {
      lobby_command("/Delete");
      continue;
    }

    else if (touch_x >= BUTTON_QUIT_X_MIN && touch_x <= BUTTON_QUIT_X_MAX &&
             touch_y >= BUTTON_QUIT_Y_MIN && touch_y <= BUTTON_QUIT_Y_MAX) {
      lobby_command("/Quit");
      continue;
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

  char *user_id = getenv("USER_ID");

  if (user_id != NULL) {
    printf("로그인한 사용자: %s\n", user_id);
  } else {
    printf("사용자 정보 없음\n");
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
  Draw_New_Form_UI();

  /* close */
  munmap(map, size);
  close(fd);

  lobby_screen();

  while (1) {
    char command[32];
    printf("\n로비 명령 입력 (/List, /Users, /Create, /Join, /Quit): ");
    scanf("%s", command);
    getchar();  // 개행 문자 제거

    lobby_command(command);  // 로비 명령어 처리
  }

  close(sfd);
  return EXIT_SUCCESS;
}