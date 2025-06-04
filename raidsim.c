/* 
 * RAID Simulator for RAID LEVEL 0, 4, 5, 10
 * Authors: Aman Chadha, Ethan Grefe, Aditya Prakash
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "disk-array.h"
#include "raidsim.h"

int verbose; 
//0 for working, -1 for failed
int *failed_disk;		//Keeps track of the number of disks which have failed by using an array
int numberofDiskFails; //indicates if at least one disk has failed ; 0 --> good and -1 --> disk failed





void stripe_address_5(int ndisks, int lba, int strip, int *disk_to_use, int *address_on_disk){
  //Calculate disk to write to for given lba
  *disk_to_use = lba % ((ndisks - 1) * strip) / strip;
  //Calculate block address on disk to write to for given lba
  *address_on_disk = lba / ((ndisks - 1) * strip) * strip + lba % strip;
  //Add 1 to the disk value to use if the parity disk comes before the specified disk
  if((*address_on_disk / strip) % ndisks <= *disk_to_use){
    *disk_to_use += 1;
  }
}



void recover_disk_4_5(disk_array_t *da, int nblocks, int ndisks, int disk){
  //Used to index through disks, and buffers
  int x,j,k;

  //Used to read data to from disks
  char *buffer;
  //Used to calculate and store recovered data in
  char *parityBuffer;

  //Check for the disk specified to have failed
  if(failed_disk[disk] == -1){
    //If multiple disks have failed, just recover as blank disk
    if(numberofDiskFails < 2){
      //Recover the disk so data can be written to it
      disk_array_recover_disk(*da,disk);

      //Allocated space for buffer and parityBuffer
      buffer = malloc(sizeof(char)*BLOCK_SIZE);
      parityBuffer = malloc(sizeof(char)*BLOCK_SIZE);

      //Recover each block on disk one at a time
      for(x = 0; x < nblocks; x++){
	//Initialize buffer to 0's
	for(j = 0; j < BLOCK_SIZE; j++){
	  buffer[j] = '\0';
	}

	//XOR data from all working disks to recover data 
	for(j = 0; j < ndisks; j++){
	  if(j != disk){
	    disk_array_read(*da,j,x,parityBuffer);
	    //XOR each byte of buffer with each byte of parityBuffer
	    for(k = 0; k < BLOCK_SIZE; k++)
	      buffer[k] = buffer[k] ^ parityBuffer[k];

	    //Write the recovered block to disk
	    disk_array_write(*da,disk,x,buffer);
	  }
	}
      }

      free(buffer);
      free(parityBuffer);
    }
    else{
      //Recover disk as empty disk if there are multiple fails
      disk_array_recover_disk(*da,disk);
    }

    //Decrement the number of failed disks
    numberofDiskFails--;
    //Reset the value of failed_disk for recovered disk
    failed_disk[disk] = 0;
  }
}



int read_5(int ndisks, int strip, int size, int lba, disk_array_t *da){
  //Used to step through the number blocks to read, buffers, and disks
  int i,j,k;
  //Allocate space for buffers to read data into along with a result to print
  char buffer[BLOCK_SIZE],parityBuffer[BLOCK_SIZE],result[5];

  //Read number of blocks specified by size
  for(i = 0; i < size; i++){
    //Get disk and block to write to using stripe_address
    int disk,block;
    stripe_address_5(ndisks,lba,strip,&disk,&block);

    //Check for specified disk to have failed
    if(failed_disk[disk] == -1){
      //Print error if multiple disks have failed
      if(numberofDiskFails > 1)
	printf("ERROR ");
      //Recover data for single disk fails
      else{
	//Initialize buffer to all 0's
	for(j = 0; j < BLOCK_SIZE; j++){
	  buffer[j] = '\0';
	}

	//XOR data from all disks for specified block
	for(j = 0; j < ndisks; j++){
	  if(!failed_disk[j]){
	    disk_array_read(*da,j,block,parityBuffer);
	    for(k = 0; k < BLOCK_SIZE; k++)
	      buffer[k] = buffer[k] ^ parityBuffer[k];
	  }
	}
      }
    }
    //Read data normally if disk has not failed
    else
      disk_array_read(*da,disk,block,buffer);

    //Write first 4 bytes of buffer into result
    for(j = 0; j < 4; j++)
      result[j] = buffer[j];
    result[4] = '\0';

    //Print data if disk is alright or is recoverable
    if(!failed_disk[disk] || numberofDiskFails < 2){
      //Print data if null was not read from disk
      if(result[0] != '\0')
	printf("%s ",result);
      //Print 0 for null reads from disk
      else
	printf("0 ");
    }

    //Increment lba to next address to be read
    lba++;
  }

  return 1;
}


int write_5(int ndisks, int strip, int size, int lba, disk_array_t *da, char *buffer){
  //Used to step through the number blocks to read, buffers, and disks
  int i,j,k,x;
  //Used to detect if a write error has occured
  int writeError = 0;

  //These buffers are used to read and write parity data from disks
  char *parityBuffer = malloc(sizeof(char) * BLOCK_SIZE); 
  char *old = malloc(sizeof(char) * BLOCK_SIZE); //Reads old data for subtractive parity
  char **parityArray = malloc(sizeof(char *) * strip); //Holds new parity data
  char **failedDiskArray = malloc(sizeof(char *) * strip);

  int disk, block; //Given to stripe, return disk and block to be written to                                                                                                                                                                                                                                                                                                                                        
  int startOfStrip = lba / ((ndisks -1) * strip) * strip; //starting block on disk of current stripe                                                                                                                                                                                                                                                                                                                
  int parityDisk = (lba / ((ndisks -1) * strip)) % ndisks; //contains index of disk being used for parity

  //Allocate space for arrays
  for(j = 0; j < strip; j++){
    parityArray[j] = malloc(sizeof(char)*BLOCK_SIZE);
    failedDiskArray[j] = malloc(sizeof(char)*BLOCK_SIZE);
  }

  //Initialize parityArray                                                            
  for(j = 0; j < strip; j++){
    disk_array_read(*da,parityDisk,startOfStrip+j,parityArray[j]);
  }

  //Initialize failedDiskArray                                                                                                                                                                                                                                                                                                                                                                                      
  if(numberofDiskFails == 1){
    //Zero out array for each block in the strip
    for(j = 0; j < strip; j++)
      for(k = 0; k < BLOCK_SIZE; k++)
	failedDiskArray[j][k] = '\0';

    //Calculate original data contained on disk and store in failedDiskArray
    for(j = 0; j < strip; j++){
      //XOR data from all remaining disks to recover lost data
      for(k = 0; k < ndisks; k++){
	if(!failed_disk[k]){
	  disk_array_read(*da,k,startOfStrip+j,parityBuffer);
	  for(x = 0; x < BLOCK_SIZE; x++)
	    failedDiskArray[j][x] = failedDiskArray[j][x] ^ parityBuffer[x];
	}
      }
    }
  }


  //Write to number of blocks specified by size
  for(i  = 0; i < size; i++){
    //get disk and block to write to using stripe_address
    stripe_address_5(ndisks,lba,strip,&disk,&block);
    //get value off disk for subtractive parity                                                                                                                                                                                                                                                                                                                                                                     
    disk_array_read(*da,disk,block,old);

    //write value to disk if not a failed disk                                                                                                                                                                                                                                                                       
    if(failed_disk[disk]){

      for(j = 0; j < BLOCK_SIZE; j++)
	//XOR new data with recovered data for subtractive parity
	old[j] = failedDiskArray[block % strip][j] ^ buffer[j];
      if(numberofDiskFails > 1)
	writeError = 1;
    }
    //write normally
    else{
      //XOR old data with new data for subtractive parity
      for(j = 0; j < BLOCK_SIZE; j++){
	old[j] = old[j] ^ buffer[j];
      }
      //write to disk                                                                                                                                                                                                                                                                                
      disk_array_write(*da,disk,block,buffer);
    }

    //calculate new value of parityArray                                                                                                                                   
    for(j = 0; j < BLOCK_SIZE; j++)
      parityArray[block % strip][j] = parityArray[block % strip][j] ^ old[j];

    //increment lba to get next address to be written to
    lba++;

    //Update parity                                                                                                                                                                                                                                               
    if(0 == lba % ((ndisks - 1)*strip) || i + 1 == size){
      //write parity to disk                                                                                                                                                                                                                                                                                    
      if(!failed_disk[parityDisk]){
	for(j = 0; j < strip; j++)
	  disk_array_write(*da, parityDisk, startOfStrip+j, parityArray[j]);
      }

      //Update startOfStrip,parityDisk,failedDiskArray, and ParityArray                                                                                                                                                                                                                                        
      startOfStrip = lba / ((ndisks -1) * strip) *strip;
      parityDisk = (lba / ((ndisks -1) * strip)) % ndisks;

      //Initialize parityArray                                                                                                                                                                                                                                                                                 
      for(j = 0; j < strip; j++){
	disk_array_read(*da,parityDisk,startOfStrip+j,parityArray[j]);
      }

      //Initialize failedDiskArray                                                                                                                                                                                                                                                                             
      if(numberofDiskFails == 1){
	for(j = 0; j < strip; j++)
	  for(k = 0; k < BLOCK_SIZE; k++)
	    failedDiskArray[j][k] = '\0';

	//XOR data from all working disks to recover data from failed disk for strip
	for(j = 0; j < strip; j++){
	  for(k = 0; k < ndisks; k++){
	    if(!failed_disk[k]){
	      disk_array_read(*da,k,startOfStrip+j,parityBuffer);
	      for(x = 0; x < BLOCK_SIZE; x++)
		failedDiskArray[j][x] = failedDiskArray[j][x] ^ parityBuffer[x];
	    }
	  }
	}
      }
    }//End update parity                                                                                                                                                                                                      
  }//Disk write loop 

  //Print error if any write errors were found
  if(writeError)
    printf("ERROR ");

  //Free memory
  free(parityBuffer);
  free(old);

  for(i = 0; i < strip; i++){
    free(parityArray[i]);
    free(failedDiskArray[i]);
  }
  free(parityArray);
  free(failedDiskArray);

  return 0;
}

i
int main( int argc, char *argv[] )
{
  char *c;
  int i,j;
  //int k,x;

  char *trace = NULL;
  int level,strip,ndisks,nblocks;

  numberofDiskFails = 0;

  level = -1;
  strip = -1;
  ndisks = -1;
  nblocks = -1;
  verbose = 0;

  //Parse command line input
  for(i = 1; i < argc; i++){
    c = argv[i];
    if(0 == strcmp(c,"-level")){
      i++;
      level = atoi(argv[i]);
    }
    else if(0 == strcmp(c,"-strip")){
      i++;
      strip = atoi(argv[i]);
    }
    else if(0 == strcmp(c,"-disks")){
      i++;
      ndisks = atoi(argv[i]);
    }
    else if(0 == strcmp(c,"-size")){
      i++;
      nblocks = atoi(argv[i]);
    }
    else if(0 == strcmp(c,"-trace")){
      i++;
      trace = argv[i];
    }
    else if(0 == strcmp(c,"-verbose")){
      verbose = 1;
    }
    else{
      printf("usage: ./raidsim -level LEVEL -strip NSTRIPS -disks NDISKS -size SIZE -trace TRACE -verbose(OPTIONAL)\n");
      return 1;
    }
  }

	

  //Check for variable not to be set
  if(level == -1 || strip == -1 || ndisks == -1 || nblocks == -1 || trace == NULL){
    printf("usage: ./raidsim -level LEVEL -strip NSTRIPS -disks NDISKS -size SIZE -trace TRACE -verbose(OPTIONAL)\n");
    return 1;
  }



  //Check for more the 1 disk for RAID 4 or 5
  if(level == 4 || level == 5){
    if(ndisks < 2){
      printf("ERROR: Must have at least 2 disks for RAID 4 and 5\n");
      return 1;
    }
  }

  //Initialize failed_disk
  failed_disk = malloc(sizeof(int) * ndisks);
  for(i = 0; i < ndisks; i++)
    failed_disk[i] = 0;

  //Initialize disk array
  disk_array_t da = disk_array_create("myvirtualdisk",ndisks,nblocks);
  if(!da) {
    fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
    return 1;
  }

  char *buffer;
  char *cmdBuffer;

  FILE *fp;
  char *tmp;
  int num_cmds,count;
  char **cmds = malloc(4*sizeof(char *));

  //open the trace file
  fp = fopen(trace,"r");
  if (fp==NULL) 
    {
      char error_message[30] = "An error has occurred\n";
      write(STDERR_FILENO, error_message, strlen(error_message));
      exit(1);
    }

  //block below analyzes the trace file
  while (!feof(fp)) 
    {
      cmdBuffer = malloc(sizeof(char)*1001);

      fgets(cmdBuffer, 1000, fp);
      strtok(cmdBuffer, "\n\r");

      if(strlen(cmdBuffer) < 1)
	{
	  continue;
	}

      printf("%s\n",cmdBuffer);

      tmp = strtok(cmdBuffer," ");

      num_cmds = 0;
      while (tmp) 
	{
	  cmds[num_cmds] = tmp;
	  num_cmds++;
	  tmp = strtok(NULL," ");
	}

      for (i = 0; i < num_cmds; i++){
	count++;


	if(!strcmp(cmds[0],"READ"))
	  {

	    switch(level){
	    	
	    case 5:
	      read_5(ndisks,strip,atoi(cmds[2]),atoi(cmds[1]),&da);
	      break;	
	    }
	  }
	else if(!strcmp(cmds[0],"WRITE"))
	  {

	    buffer = malloc(sizeof(char)*4096);

	    for(j = 0; j < BLOCK_SIZE; j += 4){
	      buffer[j] = cmds[3][0];
	      buffer[j+1] = cmds[3][1];
	      buffer[j+2] = cmds[3][2];
	      buffer[j+3] = cmds[3][3];
	    }

	    switch(level){
	   
	    case 5:
	      write_5(ndisks,strip,atoi(cmds[2]),atoi(cmds[1]),&da,buffer);
	      break;
	    
	    }

	    free(buffer);
	  }
	else if(!strcmp(cmds[0],"FAIL"))
	  {

	    if(failed_disk[atoi(cmds[1])] != -1){
	      failed_disk[atoi(cmds[1])] = -1;	  	
	      disk_array_fail_disk(da,atoi(cmds[1]));
	      numberofDiskFails++;
	    }
	  }
	else if(!strcmp(cmds[0],"RECOVER"))
	  {

	    switch(level){
	  
	    case 5:
	      recover_disk_4_5(&da,nblocks,ndisks,atoi(cmds[1]));
	      break;
	    
	    }
	  }
	else if(!strcmp(cmds[0],"END"))
	  {
	    disk_array_print_stats(da);

	    //Free Memory
	    disk_array_close(da);

	    fclose(fp);
	    free(cmdBuffer);
	    free(failed_disk);
	    free(cmds);

	    return 0;
	  }
	break;
      }
      //Free cmdBuffer
      free(cmdBuffer);
    }

  //Free Memory
  disk_array_close(da);
  fclose(fp);
  free(cmdBuffer);
  free(failed_disk);
  free(cmds);


  return 0;
}
