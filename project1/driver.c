//Collin Dreher
//Professor Farnan, CS 1550
#include "library.h"
#include <stdio.h>

int main(int argc, char** argv) {
  printf("\n*********** WELCOME TO THE GRAPHICS LIBRARY ***********\n");
  printf("INSTRUCTIONS: Selet an option below\n");
  printf("NOTE: Quit the program by hitting 'q' after drawing a shape.\n");
  printf("1) Draw rectangle\n");
  printf("2) Draw circle\n");
  int choice;
  char input_key;   //user input key
  scanf("%d", &choice);   //get choice

  if (choice == 1) {    //Draw rect
    while((input_key = getkey()) != 'q') {
      init_graphics();
      clear_screen();
      draw_rect(300, 200, 200, 100, 60);
      sleep_ms(5000);
    }
    clear_screen();
    exit_graphics();
  }
  if (choice == 2) {    //Draw circle
    while ((input_key = getkey()) != 'q') {
      init_graphics();
      clear_screen();
      draw_circle(300, 200, 50, 20);
      sleep_ms(5000);
    }
    clear_screen();
    exit_graphics();
  }
  if (choice == 3) {    //quit
    return 0;
  }

  return 0;
}
