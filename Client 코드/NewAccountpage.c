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

#define TOUCH_DEVICE "/dev/input/event1"

// 📌 라즈베리파이 화면 해상도
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// 📌 터치 장치 해상도 (터치 드라이버에 따라 다를 수 있음)
#define TOUCH_MAX_X 4096
#define TOUCH_MAX_Y 4096

#define COLOR_RED 0xff0000
#define COLOR_GREEN 0x00ff00
#define COLOR_BLUE 0x0000ff
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xffffff
#define COLOR_GRAY 0xaaaaaa
#define COLOR_LIGHTGRAY 0xdddddd

// ID 입력 필드 좌표 조정 300 / 60
#define ID_X_MIN 265
#define ID_X_MAX 565
#define ID_Y_MIN 80
#define ID_Y_MAX 140

// IDAvailable 버튼 좌표 조정 180 /60
#define IDAvailable_X_MIN 585
#define IDAvailable_X_MAX 765
#define IDAvailable_Y_MIN 80
#define IDAvailable_Y_MAX 140

// PW 입력 필드 좌표 조정
#define PW_X_MIN 265
#define PW_X_MAX 565
#define PW_Y_MIN 160
#define PW_Y_MAX 220

// PW Check 입력 필드 좌표 조정
#define PWCHECK_X_MIN 265
#define PWCHECK_X_MAX 565
#define PWCHECK_Y_MIN 240
#define PWCHECK_Y_MAX 300

// Nick Name 입력 필드 좌표 조정
#define NAME_X_MIN 265
#define NAME_X_MAX 565
#define NAME_Y_MIN 320
#define NAME_Y_MAX 380

// Nick Name Available 버튼 좌표 조정
#define NAMEAvailable_X_MIN 585
#define NAMEAvailable_X_MAX 765
#define NAMEAvailable_Y_MIN 320
#define NAMEAvailable_Y_MAX 380

// Create 버튼튼 좌표 조정 150 /60
#define CREATE_X_MIN 245
#define CREATE_X_MAX 395
#define CREATE_Y_MIN 400
#define CREATE_Y_MAX 460

// Cancel 버튼 좌표 조정
#define CANCEL_X_MIN 435
#define CANCEL_X_MAX 585
#define CANCEL_Y_MIN 400
#define CANCEL_Y_MAX 460

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

void Draw_New_Form_UI(void) {
  int center_x =
      vinfo.xres / 2 + 15;        // 화면 중심 X 좌표 (15픽셀 오른쪽으로 이동)
  int center_y = vinfo.yres / 2;  // 화면 중심 Y 좌표

  // 화면 초기화
  Lcd_Clear_Screen();

  // 박스 및 텍스트 크기 설정
  int box_width = 300;            // 입력 필드 너비
  int box_height = 60;            // 입력 필드 높이
  int button_width = 180;         // Available 버튼 너비 (기존보다 넓게 수정)
  int button_height = 60;         // 버튼 높이
  int action_button_width = 150;  // Create/Cancel 버튼 너비
  int action_button_height = 60;  // Create/Cancel 버튼 높이
  int spacing = 20;               // 각 요소 간 간격

  // "New ID" 필드
  Lcd_Printf(center_x - box_width - 100, center_y - 2 * (box_height + spacing),
             COLOR_BLACK, COLOR_WHITE, 2, 2, "New ID");
  draw_rect(center_x - box_width / 2, center_y - 2 * (box_height + spacing),
            box_width, box_height, COLOR_BLACK);  // ID 입력 박스
  draw_rect(center_x + box_width / 2 + 20,
            center_y - 2 * (box_height + spacing), button_width, button_height,
            COLOR_LIGHTGRAY);  // Available 버튼
  Lcd_Printf(center_x + box_width / 2 + 30,
             center_y - 2 * (box_height + spacing) + 15, COLOR_BLACK,
             COLOR_LIGHTGRAY, 2, 2, "Available");

  // "New PW" 필드
  Lcd_Printf(center_x - box_width - 100, center_y - (box_height + spacing),
             COLOR_BLACK, COLOR_WHITE, 2, 2, "New PW");
  draw_rect(center_x - box_width / 2, center_y - (box_height + spacing),
            box_width, box_height, COLOR_BLACK);  // PW 입력 박스

  // "PW check" 필드
  Lcd_Printf(center_x - box_width - 100, center_y, COLOR_BLACK, COLOR_WHITE, 2,
             2, "PW check");
  draw_rect(center_x - box_width / 2, center_y, box_width, box_height,
            COLOR_BLACK);  // PW 확인 박스

  // "Nick Name" 필드
  Lcd_Printf(center_x - box_width - 100, center_y + (box_height + spacing),
             COLOR_BLACK, COLOR_WHITE, 2, 2, "Nick Name");
  draw_rect(center_x - box_width / 2, center_y + (box_height + spacing),
            box_width, box_height, COLOR_BLACK);  // Nickname 입력 박스
  draw_rect(center_x + box_width / 2 + 20, center_y + (box_height + spacing),
            button_width, button_height, COLOR_LIGHTGRAY);  // Available 버튼
  Lcd_Printf(center_x + box_width / 2 + 30,
             center_y + (box_height + spacing) + 15, COLOR_BLACK,
             COLOR_LIGHTGRAY, 2, 2, "Available");

  // "Create" 버튼
  draw_rect(center_x - action_button_width - spacing,
            center_y + 2 * (box_height + spacing), action_button_width,
            action_button_height, COLOR_LIGHTGRAY);  // Create 버튼
  Lcd_Printf(center_x - action_button_width - spacing + 30,
             center_y + 2 * (box_height + spacing) + 15, COLOR_BLACK,
             COLOR_LIGHTGRAY, 2, 2, "Create");

  // "Cancel" 버튼
  draw_rect(center_x + spacing, center_y + 2 * (box_height + spacing),
            action_button_width, action_button_height,
            COLOR_LIGHTGRAY);  // Cancel 버튼
  Lcd_Printf(center_x + spacing + 30,
             center_y + 2 * (box_height + spacing) + 15, COLOR_BLACK,
             COLOR_LIGHTGRAY, 2, 2, "Cancel");
}

// 통신 관련 함수 시작
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

// 로그인 명령어
void register_command(char *command) {
  static char id[32] = "";
  static char password[32] = "";
  static char pw_check[32] = "";
  static char nickname[32] = "";

  if (strcmp(command, "/ID") == 0) {
    printf("ID 입력: ");
    scanf("%31s", id);
    getchar();
    printf("입력된 ID: %s\n", id);
  } else if (strcmp(command, "/IDAvailable") == 0) {
    if (strlen(id) == 0) {
      printf("ID를 먼저 입력하세요.\n");
      return;
    }
    snprintf(buf, MAX_BUF, "CHECK_ID %s", id);
    send_request(buf);

    memset(buf, 0, MAX_BUF);
    int len = recvfrom(sfd, buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&addr_server, &addr_server_len);
    if (len < 0) {
      perror("서버 응답 대기 중 오류 발생");
      return;
    }
    buf[len] = '\0';
    printf("\n[Server] %s\n", buf);
    if (strstr(buf, "ID available") != NULL) {
      printf("사용 가능한 ID입니다.\n");
    } else if (strstr(buf, "ID already exists") != NULL) {
      printf("이미 사용 중인 ID입니다. 다른 ID를 입력하세요.\n");
    } else {
      printf("ID 중복 확인 실패: %s\n", buf);
    }
  } else if (strcmp(command, "/PW") == 0) {
    printf("PW 입력: ");
    scanf("%31s", password);
    getchar();
    printf("입력된 PW: %s\n", password);
  } else if (strcmp(command, "/PWConfirm") == 0) {
    printf("PW 확인: ");
    scanf("%31s", pw_check);
    getchar();
    if (strcmp(password, pw_check) != 0) {
      printf("비밀번호가 일치하지 않습니다. 다시 입력하세요.\n");
      return;
    }
    printf("비밀번호 확인 완료\n");
  } else if (strcmp(command, "/Nickname") == 0) {
    printf("닉네임 입력: ");
    scanf("%31s", nickname);
    getchar();
    printf("입력된 닉네임: %s\n", nickname);
  } else if (strcmp(command, "/NicknameAvailable") == 0) {
    if (strlen(nickname) == 0) {
      printf("닉네임을 먼저 입력하세요.\n");
      return;
    }
    snprintf(buf, MAX_BUF, "CHECK_NICKNAME %s", nickname);
    send_request(buf);

    memset(buf, 0, MAX_BUF);
    int len = recvfrom(sfd, buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&addr_server, &addr_server_len);
    if (len < 0) {
      perror("서버 응답 대기 중 오류 발생");
      return;
    }
    buf[len] = '\0';
    printf("\n[Server] %s\n", buf);
    if (strstr(buf, "Nickname available") != NULL) {
      printf("사용 가능한 닉네임입니다.\n");
    } else if (strstr(buf, "Nickname already exists") != NULL) {
      printf("이미 사용 중인 닉네임입니다. 다른 닉네임을 입력하세요.\n");
    } else {
      printf("닉네임 중복 확인 실패: %s\n", buf);
    }
  } else if (strcmp(command, "/Register") == 0) {
    if (strlen(id) == 0 || strlen(password) == 0 || strlen(nickname) == 0) {
      printf("ID, 비밀번호, 닉네임을 모두 입력해야 합니다.\n");
      return;
    }

    snprintf(buf, MAX_BUF, "REGISTER %s %s %s", id, password, nickname);
    send_request(buf);

    memset(buf, 0, MAX_BUF);
    int len = recvfrom(sfd, buf, MAX_BUF - 1, 0,
                       (struct sockaddr *)&addr_server, &addr_server_len);

    if (len < 0) {
      perror("서버 응답 대기 중 오류 발생");
      return;
    }

    buf[len] = '\0';
    printf("\n[Server] %s\n", buf);

    if (strstr(buf, "Registration successful! Please login.") != NULL) {
      printf("회원가입 성공 로그인 페이지로 이동합니다\n");
      if (access("./loginpage", X_OK) == 0) {
        execl("./loginpage", "./loginpage", NULL);
        perror("로그인 페이지 실행 실패");
      } else {
        printf("실행 파일이 존재하지 않거나 실행 권한이 없습니다\n");
      }
    } else {
      printf("회원가입 실패: %s\n", buf);
    }
  } else if (strcmp(command, "/Cancel") == 0) {
    printf("회원가입이 취소되었습니다 로그인 페이지로 이동합니다\n");
    if (access("./loginpage", X_OK) == 0) {
      execl("./loginpage", "./loginpage", NULL);
      perror("로그인 페이지 실행 실패");
    } else {
      printf("실행 파일이 존재하지 않거나 실행 권한이 없습니다\n");
    }
  } else {
    printf("잘못된 명령어입니다 다시 입력하세요\n");
  }
}

/////////////////////

// 터치 장치 경로
char touch_device[64] = TOUCH_DEVICE;

// 사용자 입력 데이터 저장
char id[32] = "";
char password[32] = "";

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

void register_screen() {
  printf("\n 회원가입 페이지 \n");
  printf("[ ID ]\n");
  printf("[ PW ]\n");
  printf("[ PW 확인 ]\n");
  printf("[ 닉네임 ]\n");
  printf("[ 가입하기 ]\n");
  printf("[ 취소 ]\n");

  while (1) {
    wait_for_touch();
    printf("터치 감지 X = %d, Y = %d\n", touch_x, touch_y);

    if (touch_x >= ID_X_MIN && touch_x <= ID_X_MAX && touch_y >= ID_Y_MIN &&
        touch_y <= ID_Y_MAX) {
      register_command("/ID");
      continue;
    }

    else if (touch_x >= IDAvailable_X_MIN && touch_x <= IDAvailable_X_MAX &&
             touch_y >= IDAvailable_Y_MIN && touch_y <= IDAvailable_Y_MAX) {
      register_command("/IDAvailable");
      continue;
    }

    else if (touch_x >= PW_X_MIN && touch_x <= PW_X_MAX &&
             touch_y >= PW_Y_MIN && touch_y <= PW_Y_MAX) {
      register_command("/PW");
      continue;
    }

    else if (touch_x >= PWCHECK_X_MIN && touch_x <= PWCHECK_X_MAX &&
             touch_y >= PWCHECK_Y_MIN && touch_y <= PWCHECK_Y_MAX) {
      register_command("/PWConfirm");
      continue;
    }

    else if (touch_x >= NAME_X_MIN && touch_x <= NAME_X_MAX &&
             touch_y >= NAME_Y_MIN && touch_y <= NAME_Y_MAX) {
      register_command("/Nickname");
      continue;
    }

    else if (touch_x >= NAMEAvailable_X_MIN && touch_x <= NAMEAvailable_X_MAX &&
             touch_y >= NAMEAvailable_Y_MIN && touch_y <= NAMEAvailable_Y_MAX) {
      register_command("/NicknameAvailable");
      continue;
    }

    else if (touch_x >= CREATE_X_MIN && touch_x <= CREATE_X_MAX &&
             touch_y >= CREATE_Y_MIN && touch_y <= CREATE_Y_MAX) {
      register_command("/Register");
    }

    else if (touch_x >= CANCEL_X_MIN && touch_x <= CANCEL_X_MAX &&
             touch_y >= CANCEL_Y_MIN && touch_y <= CANCEL_Y_MAX) {
      register_command("/Cancel");
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

  // ✅ 종료 시 "LOGOUT" 메시지를 보내도록 신호 핸들러 설정
  signal(SIGINT, handle_exit);

  // ✅ PING 메시지를 보내는 스레드 시작
  pthread_t ping_thread;
  pthread_create(&ping_thread, NULL, (void *)send_ping, NULL);

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

  register_screen();

  while (1) {
    char command[32];
    printf(
        "\n 명령 입력 (/ID, /PW, /PWConfirm, /Nickname, /Register, /Cancel): ");
    scanf("%s", command);
    getchar();

    register_command(command);
  }

  close(sfd);
  return EXIT_SUCCESS;
}