#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define FBDEV	"/dev/fb0"

#define COLOR_RED 0xff0000
#define COLOR_GREEN 0x00ff00
#define COLOR_BLUE 0x0000ff
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xffffff
#define COLOR_GRAY 0xaaaaaa
#define COLOR_LIGHTGRAY 0xdddddd


#define PORT 57393
#define WIDTH 640
#define HEIGHT 480
#define BUFFER_SIZE (WIDTH * HEIGHT * 2) // YUYV 포맷의 크기
#define PACKET_SIZE 1400               // UDP 최대 패킷 크기


void draw_frame(int fd_fb, char *data, ssize_t recv_len) {
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;

	// 프레임버퍼 정보 가져오기
	if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("FBIOGET_VSCREENINFO");
		exit(EXIT_FAILURE);
	}
	if (ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("FBIOGET_FSCREENINFO");
		exit(EXIT_FAILURE);
	}

	// 프레임버퍼 메모리 매핑
	char *map_fb = mmap(0, vinfo.yres_virtual * finfo.line_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
	if (map_fb == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	// YUYV 데이터를 RGB로 변환하여 프레임버퍼에 그리기
	for (int i = 0; i < HEIGHT; i++) {
		for (int j = 0; j < WIDTH / 2; j++) {
			if (i * WIDTH + j * 2 * 2 >= recv_len) {
				break; // 수신된 데이터의 길이를 초과하지 않도록 방지
			}
			unsigned int yuyv = *(unsigned int *)(data + (i * WIDTH + j * 2) * 2);

			unsigned char U = (yuyv >> 8) & 0xff;
			unsigned char V = (yuyv >> 24) & 0xff;
			unsigned char Y0 = yuyv & 0xff;

			// 첫 번째 픽셀 (Y0, U, V)
			int R0 = Y0 + 1.4075 * (V - 128);
			int G0 = Y0 - 3455 * (U - 128) / 10000 - (7169 * (V - 128) / 10000);
			int B0 = Y0 + 17790 * (U - 128) / 10000;

			// 두 번째 픽셀 (Y1, U, V)
			unsigned char Y1 = (yuyv >> 16) & 0xff;
			int R1 = Y1 + 1.4075 * (V - 128);
			int G1 = Y1 - 3455 * (U - 128) / 10000 - (7169 * (V - 128) / 10000);
			int B1 = Y1 + 17790 * (U - 128) / 10000;

			// RGB 값을 0-255 범위로 클램핑
			R0 = (R0 < 0) ? 0 : (R0 > 255) ? 255 : R0;
			G0 = (G0 < 0) ? 0 : (G0 > 255) ? 255 : G0;
			B0 = (B0 < 0) ? 0 : (B0 > 255) ? 255 : B0;

			R1 = (R1 < 0) ? 0 : (R1 > 255) ? 255 : R1;
			G1 = (G1 < 0) ? 0 : (G1 > 255) ? 255 : G1;
			B1 = (B1 < 0) ? 0 : (B1 > 255) ? 255 : B1;

			// RGB 값을 프레임버퍼에 저장
			if (vinfo.bits_per_pixel == 32) {
				unsigned int rgb888_0 = (R0 << 16) | (G0 << 8) | B0;
				*(unsigned int *)(map_fb + ((i * vinfo.xres + j * 2) * vinfo.bits_per_pixel / 8)) = rgb888_0;

				unsigned int rgb888_1 = (R1 << 16) | (G1 << 8) | B1;
				*(unsigned int *)(map_fb + ((i * vinfo.xres + j * 2 + 1) * vinfo.bits_per_pixel / 8)) = rgb888_1;
			}
			else {
				unsigned short rgb565_0 = ((R0 >> 3) << 11) | ((G0 >> 2) << 5) | (B0 >> 3);
				*(unsigned short *)(map_fb + ((i * vinfo.xres + j * 2) * vinfo.bits_per_pixel / 8)) = rgb565_0;

				unsigned short rgb565_1 = ((R1 >> 3) << 11) | ((G1 >> 2) << 5) | (B1 >> 3);
				*(unsigned short *)(map_fb + ((i * vinfo.xres + j * 2 + 1) * vinfo.bits_per_pixel / 8)) = rgb565_1;
			}
		}
	}

	// 메모리 언매핑
	munmap(map_fb, vinfo.yres_virtual * finfo.line_length);
}


void receive_frame(int sockfd, char *frame_buffer, int buffer_size) {
	char packet_buffer[PACKET_SIZE];
	int received_bytes = 0;
	uint32_t expected_packet_number = 0;

	while (received_bytes < buffer_size) {
		ssize_t recv_len = recvfrom(sockfd, packet_buffer, PACKET_SIZE, 0, NULL, NULL);
		if (recv_len < 0) {
			perror("recvfrom error");
			break;
		}

		// 패킷 번호 확인
		uint32_t packet_number;
		memcpy(&packet_number, packet_buffer, sizeof(uint32_t));

		if (packet_number == expected_packet_number) {
			// 올바른 순서의 패킷만 복사
			int data_size = recv_len - sizeof(uint32_t);
			int copy_size = (buffer_size - received_bytes > data_size) ? data_size : (buffer_size - received_bytes);
			memcpy(frame_buffer + received_bytes, packet_buffer + sizeof(uint32_t), copy_size);
			received_bytes += copy_size;
			expected_packet_number++;
		}
		else {
			printf("Packet out of order: expected %d, got %d\n", expected_packet_number, packet_number);
		}

		printf("Received packet %d, total %d/%d\n", packet_number, received_bytes, buffer_size);
	}

	if (received_bytes == buffer_size) {
		printf("Frame received successfully\n");
	}
	else {
		fprintf(stderr, "Incomplete frame received\n");
	}
}




pid_t pid;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo *vip = &vinfo;
struct fb_fix_screeninfo *fip = &finfo;
char *map;

void Lcd_Put_Pixel_565(int xx, int yy, unsigned short value)
{
	int location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
	*(unsigned short *)(map + location) = value;
}

void Lcd_Put_Pixel(int xx, int yy, int color)
{
	int location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
	int r = color >> 16;
	int g = (color >> 8) & 0xff;
	int b = color & 0xff;
	*(unsigned short *)(map + location) = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void draw_rect(int x, int y, int w, int h, unsigned int color)
{
	int xx, yy;
	int location = 0;

	for (yy = y; yy < (y + h); yy++) {
		for (xx = x; xx < (x + w); xx++) {
			location = xx * (vip->bits_per_pixel / 8) + yy * fip->line_length;
			if (vip->bits_per_pixel == 32) { /* 32 bpp */
				*(unsigned int *)(map + location) = color;
			}
			else { /* 16 bpp */
				int r = color >> 16;
				int g = (color >> 8) & 0xff;
				int b = color & 0xff;
				*(unsigned short *)(map + location) = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
			}
		}
	}
}

////////////////////////
// bmp display 
////////////////////////

#pragma pack(push, 1)

typedef struct
{
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
}BMP_HDR;

#pragma pack(pop)

#define BMP_FILE ((BMP_HDR *)fp)

void Lcd_Print_BMP_Info(void * fp)
{
	printf("MAGIC = %c%c\n", BMP_FILE->magic[0], BMP_FILE->magic[1]);
	printf("BMP SIZE = %d\n", BMP_FILE->size_of_file);
	printf("RAW OFFSET = %d\n", BMP_FILE->offset);
	printf("DIB SIZE = %d\n", BMP_FILE->size_of_dib);
	printf("WIDTH = %d, HEIGHT = %d\n", BMP_FILE->width, BMP_FILE->height);
	printf("BPP = %d\n", BMP_FILE->bpp);
}

typedef struct
{
	unsigned char blue;
	unsigned char green;
	unsigned char red;
}PIXEL;

#define PIX565(p, x)	((((p)[x].red & 0xF8) << 8)|(((p)[x].green & 0xFC) << 3)|(((p)[x].blue) >> 3))


void Lcd_Draw_BMP_File_24bpp(int x, int y, void * fp)
{
	int xx, yy;
	unsigned int pad = (4 - ((BMP_FILE->width * 3) % 4)) % 4;
	unsigned char * pix = (unsigned char *)fp + BMP_FILE->offset;

	for (yy = (BMP_FILE->height - 1) + y; yy >= y; yy--)
	{
		for (xx = x; xx < BMP_FILE->width + x; xx++)
		{
			Lcd_Put_Pixel_565(xx, yy, PIX565((PIXEL *)pix, xx - x));
		}

		pix = pix + (BMP_FILE->width * sizeof(PIXEL)) + pad;
	}
}

void draw_bmp(const char *fname, int x, int y)
{
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

static unsigned char _first[] = { 0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19 };
static unsigned char _middle[] = { 0,0,0,1,2,3,4,5,0,0,6,7,8,9,10,11,0,0,12,13,14,15,16,17,0,0,18,19,20,21 };
static unsigned char _last[] = { 0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,0,17,18,19,20,21,22,23,24,25,26,27 };
static unsigned char cho[] = { 0,0,0,0,0,0,0,0,0,1,3,3,3,1,2,4,4,4,2,1,3,0 };
static unsigned char cho2[] = { 0,5,5,5,5,5,5,5,5,6,7,7,7,6,6,7,7,7,6,6,7,5 };
static unsigned char jong[] = { 0,0,2,0,2,1,2,1,2,3,0,2,1,3,3,1,2,1,3,3,1,1 };

#include "Fonts/ENG8X16.H"
#include "Fonts/HAN16X16.H"
#include "Fonts/HANTABLE.H"

#define 	ENG_FONT_X 		8
#define 	ENG_FONT_Y 		16

#define COPY(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)=*(A+loop);
#define OR(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)|=*(A+loop);

void Lcd_Han_Putch(int x, int y, int color, int bkcolor, int data, int zx, int zy)
{
	unsigned int first, middle, last;
	unsigned int offset, loop;
	unsigned char xs, ys;
	unsigned char temp[32];
	unsigned char bitmask[] = { 128,64,32,16,8,4,2,1 };

	first = (unsigned)((data >> 8) & 0x00ff);
	middle = (unsigned)(data & 0x00ff);
	offset = (first - 0xA1)*(0x5E) + (middle - 0xA1);
	first = *(HanTable + offset * 2);
	middle = *(HanTable + offset * 2 + 1);
	data = (int)((first << 8) + middle);

	first = _first[(data >> 10) & 31];
	middle = _middle[(data >> 5) & 31];
	last = _last[(data) & 31];

	if (last == 0)
	{
		offset = (unsigned)(cho[middle] * 640);
		offset += first * 32;
		COPY(han16x16 + offset, temp);

		if (first == 1 || first == 24) offset = 5120;
		else offset = 5120 + 704;
		offset += middle * 32;
		OR(han16x16 + offset, temp);
	}
	else
	{
		offset = (unsigned)(cho2[middle] * 640);
		offset += first * 32;
		COPY(han16x16 + offset, temp);

		if (first == 1 || first == 24) offset = 5120 + 704 * 2;
		else offset = 5120 + 704 * 3;
		offset += middle * 32;
		OR(han16x16 + offset, temp);

		offset = (unsigned)(5120 + 2816 + jong[middle] * 896);
		offset += last * 32;
		OR(han16x16 + offset, temp);
	}

	for (ys = 0; ys < 16; ys++)
	{
		for (xs = 0; xs < 8; xs++)
		{
			if (temp[ys * 2] & bitmask[xs])
			{
				if ((zx == 1) && (zy == 1)) Lcd_Put_Pixel(x + xs, y + ys, color);
				else if ((zx == 2) && (zy == 1))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + ys, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, color);
				}
				else if ((zx == 1) && (zy == 2))
				{
					Lcd_Put_Pixel(x + xs, y + 2 * ys, color);
					Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, color);
				}
				else if ((zx == 2) && (zy == 2))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, color);
				}
			}
			else
			{
				if ((zx == 1) && (zy == 1)) Lcd_Put_Pixel(x + xs, y + ys, bkcolor);
				else if ((zx == 2) && (zy == 1))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, bkcolor);
				}
				else if ((zx == 1) && (zy == 2))
				{
					Lcd_Put_Pixel(x + xs, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, bkcolor);
				}
				else if ((zx == 2) && (zy == 2))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, bkcolor);
				}
			}
		}

		for (xs = 0; xs < 8; xs++)
		{
			if (temp[ys * 2 + 1] & bitmask[xs])
			{
				if ((zx == 1) && (zy == 1))
					Lcd_Put_Pixel(x + xs + 8, y + ys, color);
				else if ((zx == 2) && (zy == 1)) {
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + ys, color);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + ys, color);
				}
				else if ((zx == 1) && (zy == 2)) {
					Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys, color);
					Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys + 1, color);
				}
				else if ((zx == 2) && (zy == 2)) {
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys + 1, color);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys + 1, color);
				}
			}
			else
			{
				if ((zx == 1) && (zy == 1)) Lcd_Put_Pixel(x + xs + 8, y + ys, bkcolor);
				else if ((zx == 2) && (zy == 1))
				{
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + ys, bkcolor);
				}
				else if ((zx == 1) && (zy == 2))
				{
					Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + (xs + 8), y + 2 * ys + 1, bkcolor);
				}
				else if ((zx == 2) && (zy == 2))
				{
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys + 1, bkcolor);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * (xs + 8), y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * (xs + 8) + 1, y + 2 * ys + 1, bkcolor);
				}
			}
		}
	}
}

void Lcd_Eng_Putch(int x, int y, int color, int bkcolor, int data, int zx, int zy)
{
	unsigned offset, loop;
	unsigned char xs, ys;
	unsigned char temp[32];
	unsigned char bitmask[] = { 128,64,32,16,8,4,2,1 };

	offset = (unsigned)(data * 16);
	COPY(eng8x16 + offset, temp);

	for (ys = 0; ys < 16; ys++)
	{
		for (xs = 0; xs < 8; xs++)
		{
			if (temp[ys] & bitmask[xs])
			{

				if ((zx == 1) && (zy == 1)) Lcd_Put_Pixel(x + xs, y + ys, color);
				else if ((zx == 2) && (zy == 1))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + ys, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, color);
				}
				else if ((zx == 1) && (zy == 2))
				{
					Lcd_Put_Pixel(x + xs, y + 2 * ys, color);
					Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, color);
				}
				else if ((zx == 2) && (zy == 2))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, color);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, color);
				}
			}
			else
			{
				if ((zx == 1) && (zy == 1)) Lcd_Put_Pixel(x + xs, y + ys, bkcolor);
				else if ((zx == 2) && (zy == 1))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + ys, bkcolor);
				}
				else if ((zx == 1) && (zy == 2))
				{
					Lcd_Put_Pixel(x + xs, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + xs, y + 2 * ys + 1, bkcolor);
				}
				else if ((zx == 2) && (zy == 2))
				{
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys + 1, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs, y + 2 * ys, bkcolor);
					Lcd_Put_Pixel(x + 2 * xs + 1, y + 2 * ys + 1, bkcolor);
				}
			}
		}
	}
}

void Lcd_Puts(int x, int y, int color, int bkcolor, char *str, int zx, int zy)
{
	unsigned data;

	while (*str)
	{
		data = *str++;
		if (data >= 128)
		{
			data *= 256;
			data |= *str++;
			Lcd_Han_Putch(x, y, color, bkcolor, (int)data, zx, zy);
			x += zx * 16;
		}
		else
		{
			Lcd_Eng_Putch(x, y, color, bkcolor, (int)data, zx, zy);
			x += zx * ENG_FONT_X;
		}
	}
}

void Lcd_Printf(int x, int y, int color, int bkcolor, int zx, int zy, char *fmt, ...)
{
	va_list ap;
	char string[256];

	va_start(ap, fmt);
	vsprintf(string, fmt, ap);
	Lcd_Puts(x, y, color, bkcolor, string, zx, zy);
	va_end(ap);
}

void Lcd_Clear_Screen(void)
{
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

void Init_String(void)
{
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
			}
			else {
				break;
			}
		}
#if 0
		for (j = 0; j < len; j++) {
			printf("%02x ", my_str[i][j]);
		}
		printf("\n");
#endif
		my_str_cnt++;
	}
}

void Font_Test(void)
{
	printf("\nFont Display\n");
	Lcd_Clear_Screen();
	Lcd_Printf(2, 2, COLOR_BLACK, COLOR_RED, 2, 2, "1234ABCD%^&*");
	Lcd_Printf(2, 100, COLOR_WHITE, COLOR_BLACK, 2, 2, "%s", my_str[MY_STR_MSG_MENU]);
	Lcd_Printf(2, 200, COLOR_BLUE, COLOR_RED, 2, 2, "%s 98", my_str[MY_STR_MSG_SCORE]);
	Lcd_Printf(2, 300, COLOR_BLACK, COLOR_WHITE, 1, 1, "%s", my_str[MY_STR_MSG_END]);
}

////////////////////////




void Draw_Chat_UI(void) {
	int screen_width = vinfo.xres; // 화면 너비
	int screen_height = vinfo.yres; // 화면 높이

	// 전체 UI 크기 설정
	int margin = 20; // 외부 여백
	int sidebar_width = 200; // 왼쪽 사이드바 너비
	int button_height = 50; // 버튼 높이
	int button_spacing = 10; // 버튼 간 간격
	int client_list_height = 5 * button_height; // Client List 박스 높이 (버튼 크기의 5배)
	int chat_area_margin = 20; // 채팅 영역 내부 여백
	int chat_width = screen_width - sidebar_width - 3 * margin; // 채팅 영역 너비
	int chat_height = screen_height - 2 * margin; // 채팅 영역 높이

	// 화면 초기화
	Lcd_Clear_Screen();

	// Client List 박스 (사이드바 상단에 독립된 큰 박스)
	draw_rect(margin, margin, sidebar_width, client_list_height, COLOR_LIGHTGRAY); // Client List 박스 배경색 연한 회색
	Lcd_Printf(margin + 20, margin + 20, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1, "Client List");

	// 버튼들 (Virtual Sharing, Screen Sharing, Quit - 회색)
	draw_rect(margin + 20, margin + client_list_height + button_spacing, sidebar_width - 40, button_height, COLOR_GRAY);
	Lcd_Printf(margin + 40, margin + client_list_height + button_spacing + 15, COLOR_BLACK, COLOR_GRAY, 1, 1, "Virtual Sharing");

	draw_rect(margin + 20, margin + client_list_height + 2 * button_spacing + button_height, sidebar_width - 40, button_height, COLOR_GRAY);
	Lcd_Printf(margin + 40, margin + client_list_height + 2 * button_spacing + button_height + 15, COLOR_BLACK, COLOR_GRAY, 1, 1, "Screen Sharing");

	draw_rect(margin + 20, margin + client_list_height + 3 * button_spacing + 2 * button_height, sidebar_width - 40, button_height, COLOR_GRAY);
	Lcd_Printf(margin + 40, margin + client_list_height + 3 * button_spacing + 2 * button_height + 15, COLOR_BLACK, COLOR_GRAY, 1, 1, "Quit");

	// 채팅 영역 (오른쪽 큰 박스 - 텍스트 중앙 배치 및 배경색 연한 회색)
	draw_rect(2 * margin + sidebar_width, margin, chat_width, chat_height, COLOR_LIGHTGRAY); // 채팅 영역 배경색 연한 회색

	// 채팅 영역 텍스트 중앙 배치
	int chat_text_x = 2 * margin + sidebar_width + (chat_width / 2) - (strlen("Chatting window") * 8 / 2); // 문자열 길이에 따라 중앙 정렬
	int chat_text_y = margin + (chat_height / 2) - 8; // 텍스트 높이를 고려한 중앙 정렬
	Lcd_Printf(chat_text_x, chat_text_y, COLOR_BLACK, COLOR_LIGHTGRAY, 1, 1, "Chatting window");
}

int main(int argc, char **argv) {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in server_addr, client_addr;
	char frame_buffer[BUFFER_SIZE];
	socklen_t addr_len = sizeof(client_addr);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	// 소켓 바인딩
	if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	// 프레임버퍼 열기
	int fd_fb = open("/dev/fb0", O_RDWR);
	if (fd_fb < 0) {
		perror("open framebuffer");
		exit(EXIT_FAILURE);
	}









	int fd;
	int size;


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
	printf("[%d] %dx%d %dbpp\n", pid, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

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


	while (1) {
		// 프레임 수신 및 출력
		receive_frame(sockfd, frame_buffer, BUFFER_SIZE);
		draw_frame(fd_fb, frame_buffer, BUFFER_SIZE);
	}

	// 소켓 및 프레임버퍼 닫기
	close(sockfd);
	close(fd_fb);
	return 0;



	return 0;
}