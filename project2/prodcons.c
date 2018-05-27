//Collin Dreher
//Professor Farnan, CS1550

#include <sys/mman.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>

/*	Semaphore struct.
		create start and end nodes for LinkedList.
*/
struct cs1550_sem {
  int value;
  struct Node *start;													//start of LL
	struct Node *end;														//end of LL
};

//down semaphore
void down(struct cs1550_sem *sem) {
 	syscall(325, sem);                          //syscall 325 = __NR_cs1550_down
 }

//up semaphore
 void up(struct cs1550_sem *sem) {
 	syscall(326, sem);                          //syscall 326 = __NR_cs1550_up
 }

//main for output
int main(int argc, int *argv[]){
  int producers = 0;
  int consumers = 0;
  int buffer = 0;

  if(argc != 4){
    printf("Illegal number of arguments! Please enter 3 arguments, try again.\n");
    return 1;
  }
  else {
    producers = atoi(argv[1]);              //# of producers - from user input.
    consumers = atoi(argv[2]);              //# of consumers - from user input.
    buffer = atoi(argv[3]);                 //buffer size - from user input.

    if(consumers == 0 || producers == 0 || buffer ==0){
      printf("Illegal entry. Please enter non-zero values for ALL THREE arguments, try again.\n");
      return 1;
    }
  }


  //Shared memory for producers and consumers.
  void * sem_ptr = mmap(NULL, sizeof(struct cs1550_sem)*3, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

  //Allow multiple processes to share same memory regions. Include inputted bufffer size.
  void *ptr = mmap(NULL, sizeof(int)*(buffer+3), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

  //Initialize integer pointers much like the malloc() project from 449.
  int *buffer_size_ptr;                      //size of buffer
  int *prod_ptr;
  int *con_ptr;
  int *buffer_ptr;                          //buffer array

  buffer_size_ptr = (int*)ptr;
  prod_ptr = (int*)ptr+1;
  con_ptr = (int*)ptr+2;
  buffer_ptr = (int*)ptr+3;

  //Reset prodcon variables.
  *buffer_size_ptr = buffer;
  *prod_ptr = 0;
  *con_ptr = 0;

  //Create empty, full, and mutex semaphores from slides.
  struct cs1550_sem *empty = (struct cs1550_sem*)sem_ptr;
  struct cs1550_sem *full = (struct cs1550_sem*)sem_ptr+1;
  struct cs1550_sem *mutex = (struct cs1550_sem*)sem_ptr+2;

  //Initialize above semaphores ==> refer to slides.
  empty->value = buffer;
  empty->start = NULL;
  empty->end = NULL;

  full->value = 0;
  full->start = NULL;
  full->end = NULL;

  mutex->value = 1;
  mutex->start = NULL;
  mutex->end = NULL;

  int i;

  //Reset buffer.
  for(i = 0; i < buffer; i++){
    buffer_ptr[i] = 0;
  }

  //Make producer produce pancakes.
  for(i = 0; i < producers; i++){
    //if child...
    if(fork() == 0){
      int pancake;
      while(1){
        down(empty);
        down(mutex);

        pancake = *prod_ptr;
        buffer_ptr[*prod_ptr] = pancake;
        printf("Chef %c Produced: Pancake%d\n", i+65, pancake);
        *prod_ptr = (*prod_ptr+1) % *buffer_size_ptr;

        up(mutex);
        up(full);

      }
    }
  }

  //Make consumer eat the pancakes.
  for(i = 0; i < consumers; i++){
    //if child...
    if(fork() == 0){
      int pancake;
      while(1){
        down(full);
        down(mutex);

        pancake = buffer_ptr[*con_ptr];
        printf("Consumer %c Consumed: Pancake%d\n", i+65, pancake);
        *con_ptr = (*con_ptr+1) % *buffer_size_ptr;

        up(mutex);
        up(empty);
      }
    }
  }

  int status;
  wait(&status);            //wait until all pancakes(processes) are completed.

  return 0;
}
