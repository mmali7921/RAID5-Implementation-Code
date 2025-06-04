#ifndef RAIDSIM_H



void stripe_address_5(int ndisks, int lba, int strip, int *disk_to_use, int *address_on_disk);



int read_5(int ndisks, int strip, int size, int lba, disk_array_t *da);


/*Write RAID Level 5: Write 1024B sized 'buffer' to 'size' number of blocks*/

int write_5(int ndisks, int strip, int size, int lba, disk_array_t *da, char *buffer);



#endif // RAIDSIM_H