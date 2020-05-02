////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_filesys.c
//  Description    : This is the implementation of the Lion Cloud device 
//                   filesystem interfaces.
//
//   Author        : *** Sung Woo Oh ***
//   Last Modified : *** 2/26/2020 ***
//

// Include files
#include <stdlib.h>
#include <string.h>
#include <cmpsc311_log.h>

// Project include files
#include <lcloud_filesys.h>
#include <lcloud_controller.h>
#include <lcloud_cache.h>
#include <lcloud_support.h>
#include <lcloud_network.h>

//bool typedef
typedef int bool;
#define true 1
#define false 0
int full = -1;


static LCloudRegisterFrame frm, rfrm, b0, b1, c0, c1, c2, d0, d1;
int numdevice; //number of devices // there are 5 devices in assign3
#define filenum 256
//#define devicenum 16
int devicenum = 0;


//LcDeviceId did;
bool isDeviceOn;

typedef struct{
    char *fname;
    LcFHandle fhandle;
    bool isopen;
    uint32_t pos;
    int flength;
    //device info <-> file 
    int wfnow;           // file's most recently wrote device number (storage index)
    int rfnow;
    LcDeviceId fdid;    // file's most recently wrote did
    int fsec;           // file's most recently wrote sector
    int fblk;           // file's most recently wrote block


}filesys;
filesys *finfo; //file structure

typedef struct{
    LcDeviceId did;
    int sec;
    int blk;
    int rsec;
    int rblk;
    char **storage;        // 0 - empty   1- allocated  2- full
    char **fileblktracker; // each block contains file handle
    uint16_t **filepostracker;  // each block contains filepos (beginning of the blk)
    int maxsec; 
    int maxblk;
    int devwritten;        // total bytes written in a device
    int devread;
    int numwritten;
    
}device;
device *devinfo;

/*********global variables**********/
int allocatedblock = 0; // number of blocks allocated
int totalblock = 0;     // total number of blocks calculated during allocation
int now = 0;            // current writing device id
int readnow = 0;         // current reading device id
int prevfilesnow = 0;   //previous file's device id
bool findnextfreedev;   // if file went back to block to fill the block, find next free device or not



////////////////////////////////////////////////////////////////////////////////
//
// Function     : getfreeblk
// Description  : iterate the storage(2d array) and find the free sector(i)&block(j) to read/write

void getfreeblk(int n, LcFHandle fh){ //argument = now 
    int i,j;

    for(i=0; i<devinfo[n].maxsec; i++){
        for(j=0; j<devinfo[n].maxblk; j++){
            if(devinfo[n].storage[i][j] == 0){
                devinfo[n].sec = i;
                devinfo[n].blk = j;
                now = n;
                return ;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : findsamefileblock
// Description  : iterate the storage(2d array) and find the blocks that has same fh

void findsamefileblock(int n, LcFHandle fh){ //argument = readnow 
    int i,j;
    n = 0;
    // filepos/256 == fblknum 
    // fblknum = everytime offset + writebytes >= 256, counter++ -> fblknum[sec][blk]


    do{
        for(i=0; i<devinfo[n].maxsec; i++){
            for(j=0; j<devinfo[n].maxblk; j++){
                if(devinfo[n].fileblktracker[i][j] == fh && devinfo[n].filepostracker[i][j]/256 == finfo[fh].pos/256){
                    devinfo[n].rsec = i;
                    devinfo[n].rblk = j;
                    readnow = n;
                    return ;
                }

            }
        }
        n++;
    }while(n<devicenum);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : findsamefileblock (for write)
// Description  : find same file block for overwriting

void wfindsamefileblock(int n, LcFHandle fh){ //argument = now 
    int i,j;
    n = 0;

    do{
        for(i=0; i<devinfo[n].maxsec; i++){
            for(j=0; j<devinfo[n].maxblk; j++){
                if(devinfo[n].fileblktracker[i][j] == fh && devinfo[n].filepostracker[i][j]/256 == finfo[fh].pos/256 && devinfo[n].storage[i][j] > 0){
                    devinfo[n].sec = i;
                    devinfo[n].blk = j;
                    now = n;
                    return ;
                }

            }
        }
        n++;
    }while(n<devicenum);
}
////////////////////////////////////////////////////////////////////////////////
//
// Function     : nextdevice
// Description  : move onto next device

void nextdevice(int *n){

    
    while(*n <devicenum){
        if(*n != full){
            if(*n>=devicenum-1){
                *n=0;
            }
            else{
                (*n)++;
            }
        }
        
        if(*n>=devicenum-1) *n=0;
        else (*n)++;

        if(*n != full) break;
    }
    
}

// void nextdevice(int *n){
//     if(*n>=devicenum-1){
//         *n=0;
//     }
//     else{
//         (*n)++;
//     }
// }

////////////////////////////////////////////////////////////////////////////////
//
// Function     : create_lcoud_registers
// Description  : create a register structure by packing the values using bit operations
//
// Inputs       : b0, b1, c0, c1, c2, d0, d1
// Outputs      : packedregs

LCloudRegisterFrame create_lcloud_registers(uint64_t cb0, uint64_t cb1, uint64_t cc0, uint64_t cc1, uint64_t cc2, uint64_t cd0, uint64_t cd1){
    uint64_t packedregs = 0x0, tempb0, tempb1, tempc0, tempc1, tempc2, tempd0, tempd1;

    tempb0 = (cb0 & 0xffff) << 60;   // 4 bit
    tempb1 = (cb1 & 0xffff) << 56;   // 4 bit
    tempc0 = (cc0 & 0xffff) << 48;   // 8 bit
    tempc1 = (cc1 & 0xffff) << 40;   // 8 bit
    tempc2 = (cc2 & 0xffff) << 32;   // 8 bit 
    tempd0 = (cd0 & 0xffff) << 16;   // 16 bit
    tempd1 = (cd1 & 0xffff);         // 16 bi

    packedregs = tempb0|tempb1|tempc0|tempc1|tempc2|tempd0|tempd1;  //packed
    return packedregs;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_lcloud_registers
// Description  : unpack the registers created with previous function(resp)
//

uint64_t extract_lcloud_registers(LCloudRegisterFrame resp){
    b0 = (resp >> 60) & 0xf;
    b1 = (resp >> 56) & 0xf;
    c0 = (resp >> 48) & 0xff;
    c1 = (resp >> 40) & 0xff;
    c2 = (resp >> 32) & 0xff;
    d0 = (resp >> 16) & 0xffff;
    d1 = (resp & 0xffff);
    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : countdevice
// Description  : count how many devices are there
//
// Inputs       : id_c1
uint8_t countdevice(uint16_t id0){
    uint16_t value = id0;
    int count=0;
    while(value != 0){
        if((value&1) == 1){
            count++;
        }
        value = value >>1;
    }
    return count;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : probeID
// Description  : check the device ID
//
// Inputs       : id_c1

uint8_t probeID(uint16_t id0){
    unsigned int temp = id0;
    unsigned int leastbit = id0 & ~(id0-1);

    //id_d0 = (id_d0 & 0xff);
    unsigned count = 0;
    // increment count until id_c1 = 0
    while(id0){
        id0 = id0 & ~(id0-1);
        id0 >>= 1;
        count++;
    }
    d0 = temp - leastbit;

    return count-1;  //shifted amount -1 will be device id
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : do_read
//
// Input        : did, sec, blk, *buf
//
// Description  : create the registers, call the io-bus, take the 64-bit value and back,
//                extract the registers, and check value 0.
//

int do_read(int did, int sec, int blk, char *buf){

    frm = create_lcloud_registers(0, 0 ,LC_BLOCK_XFER ,did, LC_XFER_READ, sec, blk); 

    if( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, buf)) == -1) || 
    (extract_lcloud_registers(rfrm)) || (b0 != 1) || (b1 != 1) || (c0 != LC_BLOCK_XFER)){
        logMessage(LOG_ERROR_LEVEL, "LC failure reading blkc [%d/%d/%d].", did, sec, blk);
        return(-1);
    }
    logMessage(LcDriverLLevel, "LC success reading blkc [%d/%d/%d].", did, sec, blk);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : do_write
//
// Input        : did, sec, blk, *buf
//
// Description  : create the registers, call the io-bus, take the 64-bit value and back,
//                extract the registers, and check value 0.
//

int do_write(int did, int sec, int blk, char *buf){

    frm = create_lcloud_registers(0, 0 ,LC_BLOCK_XFER ,did, LC_XFER_WRITE, sec, blk);  

    if( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, buf)) == -1) ||   
    (extract_lcloud_registers(rfrm)) || (b0 != 1) || (b1 != 1) || (c0 != LC_BLOCK_XFER)){ 
        logMessage(LOG_ERROR_LEVEL, "LC failure writing blkc [%d/%d/%d].", did, sec, blk);
        return(-1);
    }
    logMessage(LcDriverLLevel, "LC success writing blkc [%d/%d/%d].", did, sec, blk);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcpoweron
//
// Description  : power on the device 
//                  
//                b0/b1 - which direction / return status
//                c0    - Operation code (poweron/off/probe/transfer)
//                c1    - device id
//                c2    - read(0)/write(1)
//                d0/d1 - sector/block
//

int32_t lcpoweron(void){
    int i,j;
    int fd;
    int reserved0;

    // cache init
    lcloud_initcache(LC_CACHE_MAXBLOCKS);

    logMessage(LcControllerLLevel, "Initialzing Lion Cloud system ...");

    // Do Operation - PowerOn
    frm = create_lcloud_registers(0, 0 ,LC_POWER_ON ,0, 0, 0, 0); 
    rfrm = client_lcloud_bus_request(frm, NULL);
    extract_lcloud_registers(rfrm);

    isDeviceOn = true;
    bool first = true; //check if is calling probeID first time

    int n=0; //devinit loop counter

    // Do Operation - Devprobe
    frm = create_lcloud_registers(0, 0 ,LC_DEVPROBE ,0, 0, 0, 0); 
    rfrm = client_lcloud_bus_request(frm, NULL);
    extract_lcloud_registers(rfrm); //after extract I get probed d0 (22048)
    devicenum = countdevice(d0);
    
    devinfo = (device *)malloc(sizeof(device) * devicenum);
    for(i=0; i<devicenum; i++){
        devinfo[i].did = 0;
        devinfo[i].sec = 0;
        devinfo[i].blk = 0;
        devinfo[i].rsec = 0;
        devinfo[i].rblk = 0;
        devinfo[i].maxsec = 0;
        devinfo[i].maxblk = 0;
        devinfo[i].numwritten = 0;
        devinfo[i].devwritten = 0;
        devinfo[i].devread = 0;
    }


    //---------------------- Device init ----------------------------//
    do{ //find out each multiple devices' number
    
        if(first == true){
            devinfo[n].did = probeID(d0);

            first = false;
        }
        else{
            devinfo[n].did = probeID(reserved0); 
        }
        //logMessage(LcControllerLLevel, "Found device [%d] in cloud probe.", devinfo->did);

        frm = create_lcloud_registers(0, 0 ,LC_DEVINIT ,devinfo[n].did, 0, 0, 0); 
        rfrm = client_lcloud_bus_request(frm, NULL);
        reserved0 = d0; // reserve d0 after probeID function
        extract_lcloud_registers(rfrm);
        devinfo[n].maxsec = d0;
        devinfo[n].maxblk = d1;
        logMessage(LcControllerLLevel, "Found device [did=%d, secs=%d, blks=%d] in cloud probe.", devinfo[n].did, d0, d1);


        //------------2d array dynamic allocation----------//
        devinfo[n].storage = (char **) malloc(sizeof(char*) * devinfo[n].maxsec); //ex. did = 5,  blk = 64
        devinfo[n].fileblktracker = (char **) malloc(sizeof(char*) * devinfo[n].maxsec); //ex. did = 5,  blk = 64
        devinfo[n].filepostracker = (uint16_t **) malloc(sizeof(uint16_t*) * devinfo[n].maxsec); //ex. did = 5,  blk = 64
        for(i=0; i<devinfo[n].maxsec; i++){
            devinfo[n].storage[i] = (char *) malloc(sizeof(char) * devinfo[n].maxblk);  //ex. did = 5. sec = 10
            devinfo[n].fileblktracker[i] = (char *) malloc(sizeof(char) * devinfo[n].maxblk);  //ex. did = 5. sec = 10
            devinfo[n].filepostracker[i] = (uint16_t *) malloc(sizeof(uint16_t) * devinfo[n].maxblk);  //ex. did = 5. sec = 10
        }
        // zero out storage (device tracker)
        for(i=0; i<devinfo[n].maxsec; i++){
            for(j=0; j< devinfo[n].maxblk; j++){
                devinfo[n].storage[i][j] = 0;
                devinfo[n].fileblktracker[i][j] = 0;
                devinfo[n].filepostracker[i][j] = 0;
            }
        }
        /////////////////////////////////////////////////////

        totalblock += devinfo[n].maxsec * devinfo[n].maxblk;
    
        
        //increment index(next device)
        n++;
    }while(n<devicenum);

    ////////////////// file initialize //////////////////////
    finfo = (filesys *)malloc(sizeof(filesys) * filenum);
    for(fd=0; fd<filenum; fd++){

        finfo[fd].isopen = false;
        finfo[fd].fname = "\0";   // ' '?
        finfo[fd].pos = -1;
        finfo[fd].fhandle = -1;
        finfo[fd].flength = -1;
        //device <-> file
        finfo[fd].fdid = -1;
        finfo[fd].fsec = -1;
        finfo[fd].fblk = -1;
        finfo[fd].wfnow = -1;
        finfo[fd].rfnow = -1;
    }


    //lcloud_initcache();

    return 0;
}

// File system interface implementation

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcopen
// Description  : Open the file for for reading and writing
//
// Inputs       : path - the path/filename of the file to be read
// Outputs      : file handle if successful test, -1 if failure

LcFHandle lcopen( const char *path ) {

    int fd=0;

    //check if power is off, and poweron
    if(isDeviceOn == false){
        lcpoweron();
    }

    while(fd < filenum){
        //check if opening the file again
        if(strcmp(path, finfo[fd].fname) == 0){
            if(finfo[fd].isopen == true){
                logMessage(LOG_ERROR_LEVEL, "File is already opened.\n\n");
                return -1;
            }
        }
        //if we are opening another file, increment the file handle
        else{ 
            fd++;
            if(finfo[fd].isopen == false ) break;
        }
    }

    finfo[fd].isopen = true;
    finfo[fd].fname = strdup(path);        //save file name
    finfo[fd].fhandle = fd;                //pick unique file handle
    finfo[fd].pos = 0;                     //set file pointer to first byte
    finfo[fd].flength = 0;
    //device <-> file
    finfo[fd].fdid = 0;
    finfo[fd].fsec = 0;
    finfo[fd].fblk = 0;
    finfo[fd].wfnow = 0;
    finfo[fd].rfnow = 0;

    logMessage(LcControllerLLevel, "Opened new file [%s], fh=%d.", finfo[fd].fname, finfo[fd].fhandle);

    return(finfo[fd].fhandle);
} 

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcread
// Description  : Read data from the file 
//
// Inputs       : fh - file handle for the file to read from
//                buf - place to put the data
//                len - the length of the read
// Outputs      : number of bytes read, -1 if failure

int lcread( LcFHandle fh, char *buf, size_t len ) {

    uint32_t readbytes, filepos;
    //uint64_t blknum, secnum;
    uint16_t offset, remaining, size;
    char tempbuf[LC_DEVICE_BLOCK_SIZE];
    prevfilesnow = readnow; // save last block point

    memset(tempbuf, 0x0, LC_DEVICE_BLOCK_SIZE);
    
    /*************Error Checking****************/

    //check if file handle is valid (is associated with open file)
    if(fh < 0 || finfo[fh].isopen == false){
        logMessage(LOG_ERROR_LEVEL, "Failed to read: file handle is not valid or file is not opened");
        return -1;
    }
    //check length to if it is valid
    if(len < 0){
        logMessage(LOG_ERROR_LEVEL, "Failed to read: length is not valid");
        return -1;
    }
    //check if reading exceeds end of the file
    if(finfo[fh].pos+len > finfo[fh].flength){
        logMessage(LOG_ERROR_LEVEL, "Reading exceeds end of the file");
        return -1;
    }

    int start = finfo[fh].flength - finfo[fh].pos;
    if(len > start){
        len = start;
    }

    filepos = finfo[fh].pos;
    readbytes = len;


    /////////////// begin reading ////////////////////

    while( readbytes > 0){

        findsamefileblock(readnow, fh);  

        offset = filepos % LC_DEVICE_BLOCK_SIZE; //e.g. 50%256 = 50,  500%256 = 244 (1block and 244bytes)
        remaining = LC_DEVICE_BLOCK_SIZE - offset;  //e.g. 256-(500%256) = 12


        //if exceeds the len we will write will be the remaining
        if(readbytes < remaining){
            size = readbytes;
        }
        else{
            size = remaining;
        }

        // if found in cache, get it
        if(lcloud_getcache(devinfo[readnow].did, devinfo[readnow].rsec, devinfo[readnow].rblk) != NULL){
            memcpy(tempbuf, lcloud_getcache(devinfo[readnow].did, devinfo[readnow].rsec, devinfo[readnow].rblk), 256);
            memcpy(buf, tempbuf+offset, size);
        }

        /************ if read exceeds the block size (256) *************/
        else if(offset + readbytes >= LC_DEVICE_BLOCK_SIZE){
            // read, and copy up to len to the buf
            do_read(devinfo[readnow].did, devinfo[readnow].rsec, devinfo[readnow].rblk, tempbuf);
            memcpy(buf, tempbuf+offset, size); 
        }
        else{

            // read, and copy up to len to the buf
            do_read(devinfo[readnow].did, devinfo[readnow].rsec, devinfo[readnow].rblk, tempbuf);
            memcpy(buf, tempbuf+offset, size);
        }
    
        /////// update position, readbytes, and buf offset //////
        filepos += size;
        readbytes -= size;
        buf += size;
        devinfo->devread += size;
        finfo[fh].rfnow = readnow;      


        //nextdevice(&readnow);
    
        
        // if position exceeds the size of the file then increase file size to current position
        if(filepos > finfo[fh].flength){
            finfo[fh].flength = filepos;
        }

        finfo[fh].pos = filepos;

    }

    logMessage(LcDriverLLevel, "Driver read %d bytes to file %s", len, finfo[fh].fname, finfo[fh].flength);
    return( len );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcwrite
// Description  : write data to the file
//
// Inputs       : fh - file handle for the file to write to
//                buf - pointer to data to write
//                len - the length of the write
// Outputs      : number of bytes written if successful test, -1 if failure

int lcwrite( LcFHandle fh, char *buf, size_t len ) {

    uint64_t writebytes, filepos;
    uint16_t offset, remaining, size;
    char tempbuf[LC_DEVICE_BLOCK_SIZE];
    prevfilesnow = now; // save last block point
    
    

    /*************Error Checking****************/

    //check if file handle is valid (is associated with open file)
    if(finfo[fh].fhandle != fh || fh < 0 || finfo[fh].isopen == false){
        logMessage(LOG_ERROR_LEVEL, "Failed to write: file handle is not valid or file is not opened");
        return -1;
    }
    //check length to if it is valid
    if(len < 0){
        logMessage(LOG_ERROR_LEVEL, "Failed to write: length is not valid");
        return -1;
    }
    
    /******************Begin Writing********************/
    writebytes = len;;
    filepos = finfo[fh].pos;


    while(writebytes > 0){
        
        getfreeblk(now, fh);   // find empty block within the passed device

        if(devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] == 0){  //if writing first time on that blk
            devinfo[now].filepostracker[devinfo[now].sec][devinfo[now].blk] = filepos; //each blk remembers filepos (beginning)
        }

        // if there's block has some space left, go back to device that file lastly wrote
        if(devinfo[finfo[fh].wfnow].storage[finfo[fh].fsec][finfo[fh].fblk] == 1){
            now = finfo[fh].wfnow;
            findnextfreedev = true;
        }

        // just in case of F-15 off=925 sz=298  -> when there's block file did not finish writing but skip and tries to find empty blk in same device
        if(prevfilesnow == finfo[fh].wfnow && devinfo[finfo[fh].wfnow].storage[finfo[fh].fsec][finfo[fh].fblk] == 1){
            devinfo[now].sec = finfo[fh].fsec;
            devinfo[now].blk = finfo[fh].fblk;
        }

        // when seek brings back to position where already written
        if(filepos < finfo[fh].flength){
            wfindsamefileblock(now, fh);
            logMessage(LOG_INFO_LEVEL, "file overwrites from pos:%d", filepos);
        }
      
        
        //allocate block if block is empty
        else if(devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] < 1){
            devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] = 1;
            allocatedblock++;
            logMessage(LOG_INFO_LEVEL, "Allocated block %d out of %d (%0.2f%%)", allocatedblock, totalblock, (float)allocatedblock/(float)totalblock);
            logMessage(LcDriverLLevel, "Allocated block for data [%d/%d/%d]", devinfo[now].did, devinfo[now].sec, devinfo[now].blk);
            finfo[fh].fdid = devinfo[now].did;
            finfo[fh].fsec = devinfo[now].sec;
            finfo[fh].fblk = devinfo[now].blk;
        } 
        
        offset = filepos % LC_DEVICE_BLOCK_SIZE;  //e.g. 50%256 = 50,  500%256 = 244 (1block and 244bytes)
        remaining = LC_DEVICE_BLOCK_SIZE - offset;  //e.g. 256-(500%256) = 12


        if(devinfo[now].blk > devinfo[now].maxblk){
            logMessage(LOG_ERROR_LEVEL, "Block number exceeds memory");
            return -1;
        }
        

        //if exceeds the len we will write will be the remaining
        if(writebytes < remaining){
            size = writebytes;
        }
        else{
            size = remaining;
        }

        /************** if fills exactly 256  *****************/
        if(offset + writebytes == LC_DEVICE_BLOCK_SIZE){
            if(filepos < finfo[fh].flength){
                do_read(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf); //read to find offset
                memcpy(tempbuf+offset, buf, writebytes );
                do_write(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
                lcloud_putcache(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
            }
            else{
                do_read(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf); //read to find offset
                memcpy(tempbuf+offset, buf, writebytes );
                do_write(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
                lcloud_putcache(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
            }
            

            if(devinfo[now].sec != finfo[fh].fsec || devinfo[now].blk != finfo[fh].fblk){
                devinfo[now].storage[finfo[fh].fsec][finfo[fh].fblk] = 2;
            }
            else{
                devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] = 2; // block is full
            }
        }
        /////////// one block done ///////////


        /************* if exceeds the block size **************/
        else if(offset + writebytes > LC_DEVICE_BLOCK_SIZE){ 
            if(filepos < finfo[fh].flength){
                do_read(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf); //read to find offset
                memcpy(tempbuf+offset, buf, size );
                do_write(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
                lcloud_putcache(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
            }
            else{
                do_read(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
                memcpy(tempbuf+offset, buf, size );
                do_write(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
                lcloud_putcache(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
            }

            if(devinfo[now].sec != finfo[fh].fsec || devinfo[now].blk != finfo[fh].fblk){
                devinfo[now].storage[finfo[fh].fsec][finfo[fh].fblk] = 2;
            }
            else{
                devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] = 2; // block is full
            }
        }

        else{  //if(offset + len < 256)
            if(filepos < finfo[fh].flength){
                do_read(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf); //read to find offset
                memcpy(tempbuf+offset, buf, size );
                do_write(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
                lcloud_putcache(devinfo[now].did, devinfo[now].sec, devinfo[now].blk, tempbuf);
            }
            else{
                do_read(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
                memcpy(tempbuf+offset, buf, size ); //flength%256 instead of size?
                do_write(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
                lcloud_putcache(finfo[fh].fdid, finfo[fh].fsec, finfo[fh].fblk, tempbuf);
            }
        }
        


        if(filepos < finfo[fh].flength){
            devinfo[now].storage[devinfo[now].sec][devinfo[now].blk] = 3; // when overwritten
        }
        

        // block remembers which file wrote on it
        devinfo[now].fileblktracker[finfo[fh].fsec][finfo[fh].fblk] = finfo[fh].fhandle;

        ////////update pos, decrease len used (bytesleft to write), update buffer after written///////////////////
        filepos += size; 
        writebytes -= size;
        buf += size;
        devinfo[now].devwritten += size; // plus amount of overwritten
        finfo[fh].wfnow = now;


        // if device is full, go to next device
        // if(devinfo[now].storage[devinfo[now].maxsec-1][devinfo[now].maxblk-1] == 2){
        //     nextdevice(&now);
        // }
        if(devinfo[now].storage[devinfo[now].maxsec-1][devinfo[now].maxblk-1] == 2){
            full = now;
        }
        if(findnextfreedev == true){
            now = prevfilesnow;
        }
        else{
            nextdevice(&now);
        }
    

        // if position exceeds the size of the file then increase file size to current position
        if(filepos > finfo[fh].flength){
            finfo[fh].flength = filepos;
        }
      
        finfo[fh].pos = filepos;

        findnextfreedev = false; //reset toggle
    }
    
    logMessage(LcDriverLLevel, "Driver wrote %d bytes to file %s (now %d bytes)", len, finfo[fh].fname, finfo[fh].flength);
    return( len );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcseek
// Description  : Seek to a specific place in the file
//
// Inputs       : fh - the file handle of the file to seek in
//                off - offset within the file to seek to
// Outputs      : 0 if successful test, -1 if failure

int lcseek( LcFHandle fh, size_t off ) {
    //filesys finfo;

    if(fh < 0 || finfo[fh].isopen == false || isDeviceOn == false || finfo[fh].flength < 0 /*||(finfo[fh].pos + off) > finfo[fh].flength*/){
        logMessage(LOG_ERROR_LEVEL, "file failed to seek in");
        return -1;
    }
    if(finfo[fh].flength < off){
        logMessage(LOG_ERROR_LEVEL, "Seeking out of file [%d < %d]", finfo[fh].flength, off);
    }

    logMessage(LcDriverLLevel, "Seeking to position %d in file handle %d [%s]", off, fh, finfo[fh].fname);
    finfo[fh].pos = off;

    return( finfo[fh].pos ); //fix this 
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcclose
// Description  : Close the file
//
// Inputs       : fh - the file handle of the file to close
// Outputs      : 0 if successful test, -1 if failure

int lcclose( LcFHandle fh ) {

    //check if there is no file to close
    if(finfo[fh].isopen == false){
        logMessage(LOG_ERROR_LEVEL, "There is no opened file to close");
        return -1;
    }

    //close file
    finfo[fh].isopen = false;

    logMessage(LcDriverLLevel, "Closed file handle %d [%s]", fh, finfo[fh].fname);
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcshutdown
// Description  : Shut down the filesystem
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int lcshutdown( void ) {
    int i;

    //////////////////////// free //////////////////////////
    int n=0;
    while(n<devicenum){
        for(i = 0; i < devinfo[n].maxsec; i++){
            free(devinfo[n].storage[i]);
            free(devinfo[n].fileblktracker[i]);
            free(devinfo[n].filepostracker[i]);
        }      
        free(devinfo[n].storage);    
        free(devinfo[n].fileblktracker);
        free(devinfo[n].filepostracker);
        n++;
    }

    free(devinfo);
    ////////////////////////////////////////////////////////


    //Poweroff
    frm = create_lcloud_registers(0, 0 ,LC_POWER_OFF ,0, 0, 0, 0); 
    client_lcloud_bus_request(frm, NULL);

    // close cache
    lcloud_closecache();


    logMessage(LcDriverLLevel, "Powered off the Lion cloud system.");

    isDeviceOn = false;

    return( 0 );
}
