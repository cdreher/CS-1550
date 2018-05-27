//Collin Dreher
//Professor Farnan - CS1550

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

struct mem_reference{
  uint32_t address;
  char mode;
};

struct opt_page_table{
  uint32_t page_number;
  uint32_t next_ref;
};

struct clock_table{
  uint32_t page_number;
};

struct aging_page{
  uint32_t page_number;
  uint32_t age;
};

struct wsclock_table{
  uint32_t page_number;
  uint32_t virtual_time;
};


int isValid(uint32_t *page_table, uint32_t page){
  int is_valid;
  is_valid = page_table[page] & 1<<20;
  return is_valid;
}

void setValid(uint32_t *page_table, uint32_t page_number){
  page_table[page_number] = page_table[page_number] | 1<<20;
}

int isDirty(uint32_t *page_table, uint32_t page){
  int is_dirty;
  is_dirty = page_table[page] & 1<<21;
  return is_dirty;
}

void setDirty(uint32_t *page_table, uint32_t page_number){
  page_table[page_number] = page_table[page_number] | 1<<21;
}

int isReferenced(uint32_t *page_table, uint32_t page){
  int ref;
  ref = page_table[page] & 1<<22;
  return ref;
}

void setReferenced(uint32_t *page_table, uint32_t page_number){
  page_table[page_number] = page_table[page_number] | 1<<22;
}

void unsetReferenced(uint32_t *page_table, uint32_t page_number){
  uint32_t jawn;
  jawn = 1<<22;
  jawn = ~jawn;
  page_table[page_number] = page_table[page_number] & jawn;
}

//find next reference of current page
uint32_t find_next_reference(uint32_t page_number, struct mem_reference *instructions, uint32_t instruction_count, uint32_t start){
  uint32_t i;
  uint32_t value = 4294967295;
  for(i = start; i < instruction_count; i++){
    if((instructions[i].address>>12) == page_number){
      value = i;
      break;
    }
  }
  return value;
}

void clear_status_bits(uint32_t *page_table, uint32_t page_number){
  uint32_t jawn = 7<<20;
  jawn = ~jawn;
  page_table[page_number] = page_table[page_number] & jawn;
}

void reset_bits_aging(uint32_t *page_table, struct aging_page *temp_page_table, int validPages){
  int i;
  for(i = 0; i < validPages; i++){
    unsetReferenced(page_table, temp_page_table[i].page_number);
  }
}

//do what you gotta do to the page
void activate_page(uint32_t * page_table, uint32_t page_number, uint32_t frame, char mode){
  page_table[page_number] = frame;
  setValid(page_table, page_number);
  setReferenced(page_table, page_number);
  if(mode == 'W'){
    setDirty(page_table, page_number);
  }
}

//////////////////////////////////////
//OPT ALGORITHM
/////////////////////////////////////
void opt(struct mem_reference *instructions, uint32_t instruction_count, int numframes, uint32_t pages, uint32_t *page_table){
  int disk_writes = 0;
  int page_faults = 0;
  int nextFrame = 0;
  int max;
  int i, j;

  uint32_t page_number;
  struct opt_page_table validPages[numframes];

  printf("Beginning OPT Simulation...\n");

  for(i = 0; i < numframes; i++){
    validPages[i].page_number = 4294967295;
  }

  for(i = 0; i < instruction_count; i++){
    page_number = instructions[i].address >> 12;

    if(!isValid(page_table, page_number)){
      page_faults++;
      if(nextFrame == numframes){     //if no frames are left to add pages...
        max = 0;
        for(j = 0; j < nextFrame; j++){
          if(validPages[j].next_ref > validPages[max].next_ref){
            max = j;
          }
          else if(validPages[j].next_ref == validPages[max].next_ref){
            if(!isDirty(page_table, validPages[max].page_number)){
              max = j;
            }
          }
        }
        if(isDirty(page_table, validPages[max].page_number)){
          printf("%d. Page Fault - evict dirty\n", i);
          disk_writes++;
        }
        else{
          printf("%d. Page Fault - evict clean\n", i);
        }

        clear_status_bits(page_table, validPages[max].page_number);

        validPages[max].page_number = page_number;
        validPages[max].next_ref = find_next_reference(page_number, instructions, instruction_count, i+1);
        activate_page(page_table, page_number, max, instructions[i].mode);
      }
      else{     //if frames are free...
        printf("%i. no eviction\n", i);
        validPages[nextFrame].page_number = page_number;
        //find next reference of page
        validPages[nextFrame].next_ref = find_next_reference(page_number, instructions, instruction_count, i+1);
        activate_page(page_table, page_number, nextFrame, instructions[i].mode);
        nextFrame++;
      }
    }
    else{
      printf("%d. hit\n", i);

      validPages[page_table[page_number] & 1048575].next_ref = find_next_reference(page_number, instructions, instruction_count, i+1);

      if(instructions[i].mode == 'W'){
        setDirty(page_table, page_number);
      }
    }
  }

  //Print out results.
  printf("OPT\n");
  printf("Number of frames: %d\n", numframes);
  printf("Total memory accesses: %d\n", instruction_count);
  printf("Total page faults: %d\n", page_faults);
  printf("Total writes to disk: %d\n", disk_writes);

}

//////////////////////////////////////
//CLOCK ALGORITHM
/////////////////////////////////////
void clock_algo(struct mem_reference *instructions, uint32_t instruction_count, int numframes, uint32_t pages, uint32_t *page_table){
  int disk_writes = 0;
  int page_faults = 0;
  int i,j;
  int place = 0;
  int filled_frames = 0;
  uint32_t page_number;

  struct clock_table *temp_page_table;
  temp_page_table = malloc(numframes * sizeof(struct clock_table));

  printf("Beginning CLOCK Simulation...\n");

  for(i = 0; i < numframes; i++){
    temp_page_table[i].page_number = 4294967295;
  }

  for(i = 0; i < instruction_count; i++){
    page_number = instructions[i].address >> 12;
    // printf("\npage #: %d\n", page_number);
    // printf("place: %d\n", place);
    if(!isValid(page_table, page_number)){
      page_faults++;

      if(filled_frames == numframes){
        if(isDirty(page_table, temp_page_table[place].page_number)){
            printf("%i. Page Fault - evict dirty\n", i);
            disk_writes++;
        }
        else{
          printf("%i. Page Fault - evict clean\n", i);
        }

        clear_status_bits(page_table, temp_page_table[place].page_number);

        temp_page_table[place].page_number = page_number;
        activate_page(page_table, page_number, place, instructions[i].mode);
        place = (place+1) % numframes;
      }
      else{
        printf("%i. no eviction\n", i);
        filled_frames++;
        temp_page_table[place].page_number = page_number;
        activate_page(page_table, page_number, place, instructions[i].mode);
        printf("index: %d\n", place);
        //printf("page #: %d\n", page_number);
        place = (place+1) % numframes;
        //printf("\nnew place: %d\n", place);
      }
    }
    else{
      printf("%i. Hit.\n", i);

      setReferenced(page_table, page_number);

      if(instructions[i].mode == 'W'){
        setDirty(page_table, page_number);
      }
    }
  }

  //Print out results.
  printf("\nCLOCK\n");
  printf("Number of frames: %d\n", numframes);
  printf("Total memory accesses: %d\n", instruction_count);
  printf("Total page faults: %d\n", page_faults);
  printf("Total writes to disk: %d\n", disk_writes);
}

//////////////////////////////////////
//AGING ALGORITHM
/////////////////////////////////////
void aging(struct mem_reference *instructions, uint32_t instruction_count, int numframes, uint32_t pages, uint32_t *page_table, int refresh){
  struct aging_page *temp_page_table;
  temp_page_table = malloc(numframes * sizeof(struct aging_page));

  int disk_writes = 0;
  int page_faults = 0;
  int validPages = 0;
  int evict = 0;
  int i, j;

  uint32_t page_number;

  printf("Beginning AGING Simulation...\n");

  for(i = 0; i < instruction_count; i++){
    if(i % refresh == 0){
      for(j = 0; j < validPages; j++){
        temp_page_table[j].age = temp_page_table[j].age >> 1;       //shift referenced right one (divide by 2).
        if(isReferenced(page_table, temp_page_table[j].page_number)){
          uint32_t ref = isReferenced(page_table, temp_page_table[j].page_number);
          printf("test - %d\n", ref);
          temp_page_table[j].age = temp_page_table[j].age | (1<<7);
        }
      }
      reset_bits_aging(page_table, temp_page_table, validPages);
    }
    page_number = instructions[i].address >> 12;
    if(!isValid(page_table, page_number)){
      page_faults++;
      if(validPages < numframes){
        printf("%i. Page Fault - no eviction\n", i);
        temp_page_table[validPages].page_number = page_number;
        temp_page_table[validPages].age = 0;
        activate_page(page_table, page_number, validPages, instructions[i].mode);
        validPages++;
      }
      else{
        evict = 0;
        for(j = 0; j < numframes; j++){
          if(temp_page_table[j].age > temp_page_table[evict].age){
            evict = j;
          }
          else if(temp_page_table[j].age == temp_page_table[evict].age){
            if(!isDirty(page_table, temp_page_table[j].page_number)){
              evict = j;
            }
          }
        }
        if(isDirty(page_table, temp_page_table[evict].page_number)){
          printf("%i. Page Fault - evict dirty\n", i);
          disk_writes++;
        }
        else{
          printf("%i. Page Fault - evict clean\n", i);
        }
        clear_status_bits(page_table, temp_page_table[evict].page_number);
        temp_page_table[evict].page_number = page_number;
        temp_page_table[evict].age = 0;
        activate_page(page_table, page_number, evict, instructions[i].mode);
      }
    }
    else{
      printf("%i. Hit.\n", i);

      setReferenced(page_table, page_number);

      if(instructions[i].mode == 'W'){
        setDirty(page_table, page_number);
      }
    }
  }

  //Print out results.
  printf("\nAGING\n");
  printf("Number of frames: %d\n", numframes);
  printf("Total memory accesses: %d\n", instruction_count);
  printf("Total page faults: %d\n", page_faults);
  printf("Total writes to disk: %d\n", disk_writes);

}

//////////////////////////////////////
//WSCLOCK ALGORITHM
/////////////////////////////////////
void wsclock(struct mem_reference *instructions, uint32_t instruction_count, int numframes, uint32_t pages, uint32_t *page_table, int tau){
  int disk_writes = 0;
  int page_faults = 0;
  int i,j;
  int filled_frames = 0;
  int found;
  int current_frame = 0;
  int oldest_frame = 0;
  uint32_t oldest_virtual_time = 0;

  uint32_t page_number;
  struct wsclock_table *frames;
  frames = malloc(numframes * sizeof(struct wsclock_table));

  printf("Beginning WSCLOCK Simulation...\n");

  for(i = 0; i < instruction_count; i++){
    page_number = instructions[i].address >> 12;
    // printf("\npage #: %d\n", page_number);
    // printf("place: %d\n", place);
    //int valid = instructions[i].address & 1<<20;
    if(!isValid(page_table, page_number)){
      //printf("test\n");
      page_faults++;

      if(filled_frames == numframes){
        current_frame = current_frame % numframes;
        page_faults++;

        //printf("current - %d\n", current_frame);


        for(j = current_frame; j < numframes; j++){
          page_faults++;
          int valid = isValid(page_table, frames[j].page_number);
          int dirty = instructions[i].address & 1<<21;
          int ref = frames[j].page_number & 1<<22;


            //page_faults++;
            if(frames[j].virtual_time > oldest_virtual_time){
              //printf("test\n");
              oldest_virtual_time = frames[j].virtual_time;
              oldest_frame = j;
            }

            if(ref != 0){
              printf("test2\n");
              frames[j].virtual_time = i;
            }
            else if(ref == 0 && frames[j].virtual_time > tau && dirty == 0){
              printf("test3\n");
              frames[j].page_number = page_number;
              //found = 1;
              activate_page(page_table, page_number, j, instructions[i].mode);
              page_faults++;
              break;
            }
            else if(ref == 0 && dirty != 0){
              printf("test4\n");
              //unsetReferenced(page_table, frames[j].page_number);
              disk_writes++;
              printf("%i. Page Fault - evict dirty\n", i);
            }
            else{
              printf("%i. Page Fault - evict clean\n", i);
            }

            current_frame = (current_frame + 1) % numframes;


          //printf("%i %i\n", dirty, ref);


        }

        if(found == 0){
          //clear_status_bits(page_table, frames[oldest_frame].page_number);
          frames[oldest_frame].page_number = page_number;
          activate_page(page_table, page_number, oldest_frame, instructions[i].mode);
          page_faults ++;
          //frames[oldest_frame].virtual_time = oldest_virtual_time;
        }
        //clear_status_bits(page_table, frames[current_frame].page_number);
        //frames[current_frame].page_number = page_number;


        oldest_frame = 0;
        found = 0;
        oldest_virtual_time = 0;
        // else{
        //   //clear_status_bits(page_table, frames[current_frame].page_number);
        //   activate_page(page_table, page_number, current_frame, instructions[i].mode);
        // }


      }
      else{
        printf("%i. no eviction\n", i);
        frames[current_frame].page_number = page_number;
        //frames[current_frame].virtual_time = i;
        activate_page(page_table, page_number, filled_frames, instructions[i].mode);
        printf("%d %d\n", current_frame, filled_frames);
        current_frame++;
        filled_frames++;


        //printf("index: %d\n", place);
        //printf("page #: %d\n", page_number);
        //printf("\nnew place: %d\n", place);
      }

    }
    else{
      printf("%i. Hit.\n", i);

      setReferenced(page_table, page_number);

      if(instructions[i].mode == 'W'){
        setDirty(page_table, page_number);
      }
    }
  }



  //Print out results.
  printf("\nWSCLOCK\n");
  printf("Number of frames: %d\n", numframes);
  printf("Total memory accesses: %d\n", instruction_count);
  printf("Total page faults: %d\n", page_faults);
  printf("Total writes to disk: %d\n", disk_writes);
}

//////////////////////////////////////
//PROGRAM MAIN
/////////////////////////////////////
int main(int argc, char *argv[]){
  int numframes = 0;
  int refresh = 0;
  int tau = 0;
  char algorithm[128];
  char tracefile[128];
  int i;
  FILE * file;

  struct stat buf;
  uint32_t file_size;
  uint32_t instruction_count;

  struct mem_reference *instructions;
  int address;
  char mode;

  //create pages/page table and allocate necessary memory.
  uint32_t pages = 1<<20;
  uint32_t *page_table = malloc(pages * 4);
  memset(page_table, 0, pages * 4);

  //READ COMMAND LINE ARGUMENTS
  for(i = 0; i < argc; i++){
    if(strcmp(argv[i], "-n") == 0){
      numframes = atoi(argv[i+1]);
      // printf("%d\n", numframes);

      //check if frame # is valid
      if(numframes != 8  && numframes != 16 && numframes != 32 && numframes != 64 && numframes != 128){
        printf("Please enter a frame number that is either 8, 16, 32, 64, or 128. Try again.\n");
        return 0;
      }
    }
    else if(strcmp(argv[i], "-a") == 0){
      strcpy(algorithm, argv[i+1]);
      // printf("%s\n", algorithm);

    }
    else if(strcmp(argv[i], "-r") == 0){
      refresh = atoi(argv[i+1]);
      // printf("%d\n", refresh);

    }
    else if(strcmp(argv[i], "-t") == 0){
      tau = atoi(argv[i+1]);
      // printf("%d\n", tau);

    }
    else if(i == argc-1){
      strcpy(tracefile, argv[i]);
      // printf("%s\n", tracefile);

    }
  }

  //Get size of trace file to see how many instructions we have.
  stat(tracefile, &buf);
  file_size = buf.st_size;
  instruction_count = file_size / 11;
  //printf("%d\n", instruction_count);

  //open file
  file = fopen(tracefile, "r+");
  if(file == NULL){
    printf("Error! File name does not exist!\n");
    return 0;
  }

  //create array of instructions.
  instructions = malloc(instruction_count * sizeof(struct mem_reference));

  for(i = 0; i < instruction_count; i++){
      fscanf(file, "%x %c", &address, &mode);
      instructions[i].address = address;
      instructions[i].mode = mode;
  }


  //OPEN CORRECT PAGE REPLACEMENT ALGORITHM
  if(strcmp(algorithm, "opt") == 0){
    //printf("hello this is a test\n");
    opt(instructions, instruction_count, numframes, pages, page_table);
  }
  else if(strcmp(algorithm, "clock") == 0){
    clock_algo(instructions, instruction_count, numframes, pages, page_table);
  }
  else if(strcmp(algorithm, "aging") == 0){
    aging(instructions, instruction_count, numframes, pages, page_table, refresh);
  }
  else if(strcmp(algorithm, "wsclock") == 0){
    wsclock(instructions, instruction_count, numframes, pages, page_table, tau);
  }
  else{
    printf("Something went wrong. Try again.\n");
    return 0;
  }

  fclose(file);
  free(instructions);
  return 0;
}
