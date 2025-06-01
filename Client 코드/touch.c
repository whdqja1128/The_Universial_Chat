#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SCREEN_WIDTH 800   // 보드 해상도 Width
#define SCREEN_HEIGHT 480  // 보드 해상도 Height

// 🔹 실제 원본 좌표의 최소/최대값
#define RAW_X_MIN 176
#define RAW_X_MAX 3954
#define RAW_Y_MIN 383
#define RAW_Y_MAX 3883

// 🔹 X 좌표 변환 함수 (0 ~ 800)
int scale_x(int raw_x) {
  return (raw_x - RAW_X_MIN) * SCREEN_WIDTH / (RAW_X_MAX - RAW_X_MIN);
}

// 🔹 Y 좌표 변환 함수 (0 ~ 480)
int scale_y(int raw_y) {
  return (raw_y - RAW_Y_MIN) * SCREEN_HEIGHT / (RAW_Y_MAX - RAW_Y_MIN);
}

int main(int argc, char *argv[]) {
  int fd, ret;
  char *dev_name;
  struct input_event ev;
  int touch_x = -1, touch_y = -1;

  if (argc != 2) {
    printf("Usage: %s <input device>\n", argv[0]);
    return EXIT_FAILURE;
  }

  dev_name = argv[1];

  // 터치 디바이스 열기
  fd = open(dev_name, O_RDONLY);
  if (fd == -1) {
    printf("touchtest: Failed to open %s (%s)\n", dev_name, strerror(errno));
    return EXIT_FAILURE;
  }
  printf("✅ Touch Device Opened: %s\n", dev_name);
  sleep(1);

  for (;;) {
    ret = read(fd, &ev, sizeof(struct input_event));
    if (ret == -1) {
      printf("touchtest: Read Error (%s)\n", strerror(errno));
      return EXIT_FAILURE;
    }

    // 터치 좌표 감지
    if (ev.type == EV_ABS) {
      if (ev.code == ABS_X) {
        touch_x = ev.value;
      } else if (ev.code == ABS_Y) {
        touch_y = ev.value;
      }
    }

    // 터치 이벤트 발생 (BTN_TOUCH로 눌림 감지)
    if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 1) {
      if (touch_x != -1 && touch_y != -1) {
        int screen_x = scale_x(touch_x);
        int screen_y = scale_y(touch_y);

        printf("📍 Touch at: X = %d, Y = %d (Raw: %d, %d)\n", screen_x,
               screen_y, touch_x, touch_y);
      }
    }
  }

  close(fd);
  return EXIT_SUCCESS;
}