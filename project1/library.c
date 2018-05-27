//Collin Dreher
//Professor Farnan, CS 1550
#include "library.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <linux/fb.h>

//Global vars
int fd;
typedef unsigned short color_t;
unsigned long y_length;
unsigned long x_length;
unsigned long line_length;
unsigned long size;
unsigned short *p;

//syscalls used: open, ioctl, mmap
void init_graphics() {
  struct fb_var_screeninfo var_info;
  struct fb_fix_screeninfo fix_info;
  struct termios tm;

  fd = open("/dev/fb0", O_RDWR);    //open graphics device to read and write
  ioctl(fd, FBIOGET_VSCREENINFO, &var_info);    //ioctl query request for virtual resolution
  ioctl(fd, FBIOGET_FSCREENINFO, &fix_info);    //ioctl query request for bit depth

  x_length = var_info.xres_virtual;
  y_length = var_info.yres_virtual;    //yres_virtual field of first struct
  line_length = fix_info.line_length;        //line_length field of second struct
  size = y_length * line_length;

  p = (unsigned short *)mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);    //put mmap file into pointer

  //Disable keypress echo and buffering keypresses.
  ioctl(STDIN_FILENO, TCGETS, &tm);
  tm.c_lflag &= ~ECHO;    //disable echo.
  tm.c_lflag &= ~ICANON;   //disable canonical
  ioctl(STDIN_FILENO, TCSETS, &tm);
}

//syscalls used: ioctl
void exit_graphics() {
  struct termios tm;

  size = y_length * line_length;
  munmap(p, size);    //unmap file from address mem.

  //Activate echo and buffering again.
  ioctl(STDIN_FILENO, TCGETS, &tm);
  tm.c_lflag |= ECHO;   //activate echo
  tm.c_lflag |= ICANON;   //activate canonical
  ioctl(STDIN_FILENO, TCSETS, &tm);

  close(fd);    //close file
}

//syscalls used: write
void clear_screen() {
  write(1, "\033[2J", 8);    //print string "\033[2J" to tell terminal to clear itself
}

//syscalls used: select, read
char getkey() {
  char input;
  fd_set readfds;
  struct timeval timeout;   //used for elapsed time.

  FD_ZERO(&readfds);
  FD_SET(0, &readfds);

  //set timeout interval for waiting for input.
  timeout.tv_sec = 10;    //elapsed time
  timeout.tv_usec = 0;

  int select_r = select(STDIN_FILENO+1, &readfds, NULL, NULL, &timeout);    //get # of ready descriptors (in this case, just the readfs)
  if (select_r > 0) {
    read(0, &input, sizeof(input));   //read input if there is one available.
  }

  return input;
}

//syscalls used: nanosleep
void sleep_ms(long ms) {
  struct timespec ts;   //used for elapsed time - like timeval.
  ts.tv_sec = 0;
  ts.tv_nsec = ms * 1000000;
  nanosleep(&ts, NULL);
}

void draw_pixel(int x, int y, color_t color) {
  unsigned long horizontal = x;
  unsigned long vertical = (line_length/2) * y;
  unsigned short *pointer = (p + vertical + horizontal);    //pointer arithmetic
  *pointer = color;   //make pixel colored
}

void draw_rect(int x1, int y1, int width, int height, color_t c) {
    //Make rectangle with corners as followed:
    //THIS ONLY DRAWS THE CORNER YOU IDIOT!!! DO THE REST WITH A LOOP!!
    int x, y;
    for( x = x1; x < x1+width; x++) {
      for ( y = y1; y < y1+height; y++) {
        if(x == x1 || x == x1+width-1 || y == y1 || y == y1+height-1) {
          draw_pixel(x, y, c);
        }
      }
    }
}

void draw_circle(int x, int y, int r, color_t color) {
    //Draw circle using midpoint circle algorithm from: "http://en.wikipedia.org/wiki/Midpoint_circle_algorithm#Example"
    int x2 = r-1;
    int y2 = 0;
    int dx = 1;
    int dy = 1;
    int err = dx - (r << 1);

    while (x2 >= y2) {
      draw_pixel(x + x2, y + y2, color);
      draw_pixel(x + y2, y + x2, color);
      draw_pixel(x - y2, y + x2, color);
      draw_pixel(x - x2, y + y2, color);
      draw_pixel(x - x2, y - y2, color);
      draw_pixel(x - y2, y - x2, color);
      draw_pixel(x + y2, y - x2, color);
      draw_pixel(x + x2, y - y2, color);

      if (err <= 0) {
        y2++;
        err += dy;
        dy += 2;
      } else {
        x2--;
        dx += 2;
        err += dx - (r << 1);
      }
    }
}
