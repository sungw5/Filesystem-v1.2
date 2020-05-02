////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_cache.c
//  Description    : This is the cache implementation for the LionCloud
//                   assignment for CMPSC311.
//
//   Author        : Sung Woo Oh
//   Last Modified : Thu 19 Mar 2020 09:27:55 AM EDT
//

// Includes 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <cmpsc311_log.h>
#include <lcloud_cache.h>
#include <lcloud_controller.h>
#include <lcloud_filesys.h>

// cache system
typedef struct cachesys{
    char cacheblock[LC_DEVICE_BLOCK_SIZE];
    unsigned int cacheline; 
    LcDeviceId did;
    int sec;
    int blk;
    int howold; // keep track of how old is the cache


}cachesys;
cachesys *cacheinfo;

// collect cache data
typedef struct{
    int hits;
    int misses;
    int numaccess;
    int bytesused; 
    int numitem; // # of cache items
    int currentLRU;
    int currentLRUage;
}cachedata;
cachedata cdata;

int cachesize; // current cache size
int maxblock;
int mostrecent=0;


////////////////////////////////////////////////////////////////////////////////
//
// Function     : findLRU
// Description  : Search the LRU cache and return its index
//
// Outputs      : index of current LRU cache

int findLRU(){
    int i;

    cdata.currentLRUage = cacheinfo[maxblock-1].howold;
    for(i=maxblock-1; i>=0; i--){
        // find oldest cache item from the end (smallest index)
        if(cdata.currentLRUage > cacheinfo[i].howold){
            cdata.currentLRUage = cacheinfo[i].howold; //update current LRU value as oldest time
            cdata.currentLRU = i;
        }
    }
    return cdata.currentLRU;

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : findcache
// Description  : Search the cache, if not there, return NULL
//
// Outputs      : 1 or NULL 

int findcache(LcDeviceId did, uint16_t sec, uint16_t blk){
    int i;
    for(i=0; i<cachesize; i++){
        if(cacheinfo[i].did == did && cacheinfo[i].sec == sec && cacheinfo[i].blk == blk){
            return 1;
        }
    }

    return 0;
}


//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_getcache
// Description  : Search the cache for a block 
//
// Inputs       : did - device number of block to find
//                sec - sector number of block to find
//                blk - block number of block to find
// Outputs      : cache block if found (pointer), NULL if not or failure

char * lcloud_getcache( LcDeviceId did, uint16_t sec, uint16_t blk ) {
    int i;
    mostrecent++;
    
    for(i=0; i<cachesize; i++){
        // if cache exists return block, otherwise get out returning NULL
        if(cacheinfo[i].did == did && cacheinfo[i].sec == sec && cacheinfo[i].blk == blk){
            cdata.hits++; cdata.numaccess++;
            logMessage(LOG_INFO_LEVEL, "Getting found cache item on index %d, length %d", i, LC_DEVICE_BLOCK_SIZE);
            logMessage(LOG_INFO_LEVEL, "[INFO] LionCloud Cache ** HIT ** : (%d/%d/%d) index = %d", did, sec, blk, i);
            logMessage(LOG_INFO_LEVEL, "LC success getting blk [%d/%d/%d] from cache.", did, sec, blk);
            return cacheinfo[i].cacheblock; // return the found block
        }
    }
    
    // fail to find cache
    cdata.misses++; cdata.numaccess++;
    logMessage(LOG_INFO_LEVEL, "Getting cache item (not found!)");
    logMessage(LOG_INFO_LEVEL, "LionCloud Cache ** MISS ** : (%d/%d/%d)", did, sec, blk);
    /* Return not found */
    return( NULL );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_putcache
// Description  : Put a value in the cache 
//
// Inputs       : did - device number of block to insert
//                sec - sector number of block to insert
//                blk - block number of block to insert
// Outputs      : 0 if succesfully inserted, -1 if failure

int lcloud_putcache( LcDeviceId did, uint16_t sec, uint16_t blk, char *block ) {
    int i;
    int LRU;
    mostrecent++;

    for(i=0; i<maxblock; i++){

        /*************** if cache exists, update the cache ***************/
        if(cacheinfo[i].did == did && cacheinfo[i].sec == sec && cacheinfo[i].blk == blk){
            cdata.hits++; cdata.numaccess++;
            cacheinfo[i].cacheline = i;
            cacheinfo[i].howold = mostrecent;
            logMessage(LOG_INFO_LEVEL, "Getting found cache item on index %d, length %d", i, LC_DEVICE_BLOCK_SIZE);
            logMessage(LOG_INFO_LEVEL, "Removing found cache item on index %d, length %d", i, LC_DEVICE_BLOCK_SIZE );
            memcpy(cacheinfo[i].cacheblock, block, LC_DEVICE_BLOCK_SIZE); // update cache with new writing data
            return 0;
        }
    }


    /************** check if the cache is full -> LRU replacement **************/
    if(cachesize == LC_CACHE_MAXBLOCKS){
        cdata.misses++; cdata.numaccess++;
        LRU = findLRU();
        cacheinfo[LRU].cacheline = LRU;
        cacheinfo[LRU].howold = mostrecent;
        
        // set inserting cache info
        cacheinfo[LRU].did = did;
        cacheinfo[LRU].sec = sec;
        cacheinfo[LRU].blk = blk;
        memcpy(cacheinfo[LRU].cacheblock, block, LC_DEVICE_BLOCK_SIZE); // update LRU cache with new data

        logMessage(LOG_INFO_LEVEL, "Getting cache item (not found!)");
        logMessage(LOG_INFO_LEVEL, "Ejecting cache item index %d, length %d", LRU, LC_DEVICE_BLOCK_SIZE);
        logMessage(LOG_INFO_LEVEL, "Cache state [%d items, %d bytes used]", cdata.numitem, cdata.bytesused);
        logMessage(LOG_INFO_LEVEL, "Added cache item index %d, length %d", LRU, LC_DEVICE_BLOCK_SIZE);
        logMessage(LOG_INFO_LEVEL, "LionCloud Cache success inserting cache item (%d/%d/%d) index= %d", did,sec,blk,LRU);
    }


    /************* if cache does not exist, insert cache at the end **************/
    else{
        
        cdata.misses++; cdata.numaccess++;
        cacheinfo[cachesize].cacheline = cachesize;
        cacheinfo[cachesize].howold = mostrecent;
        cdata.numitem += 1; // increment the number of cache item
        

        // set inserting cache info
        cacheinfo[cachesize].did = did;
        cacheinfo[cachesize].sec = sec;
        cacheinfo[cachesize].blk = blk;
        memcpy(cacheinfo[cachesize].cacheblock, block, LC_DEVICE_BLOCK_SIZE); //put data into the cache
        cdata.bytesused += sizeof(cacheinfo[cachesize].cacheblock);

        cachesize++; // increment the cache size
    
        logMessage(LOG_INFO_LEVEL, "Getting cache item (not found!)");
        logMessage(LOG_INFO_LEVEL, "Cache state [%d items, %d bytes used]", cdata.numitem, cdata.bytesused);
        logMessage(LOG_INFO_LEVEL, "Added cache item index %d, length %d", cachesize, LC_DEVICE_BLOCK_SIZE);
        logMessage(LOG_INFO_LEVEL, "LionCloud Cache success inserting cache item (%d/%d/%d) index= %d", did,sec,blk,cachesize);
    }
    
    
    
    /* Return successfully */
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_initcache
// Description  : Initialze the cache by setting up metadata a cache elements.
//
// Inputs       : maxblocks - the max number number of blocks 
// Outputs      : 0 if successful, -1 if failure

int lcloud_initcache( int maxblocks ) {

    int i=0;

    logMessage(LOG_INFO_LEVEL, "init_cmpsc311_cache: initialization complete [%d/%d]", LC_CACHE_MAXBLOCKS, LC_CACHE_MAXBLOCKS*LC_DEVICE_BLOCK_SIZE);
    logMessage(LOG_INFO_LEVEL, "Cache state [%d items, %d bytes used]", cdata.numitem, cdata.bytesused);
    
    // cache info initialization
    cacheinfo = (cachesys *)malloc(sizeof(cachesys) * maxblocks); // 64 cache lines
    while(i<maxblocks){
        cacheinfo[i].did = -1;
        cacheinfo[i].sec = -1;
        cacheinfo[i].blk = -1;
        cacheinfo[i].howold = 0;
        memset(cacheinfo[i].cacheblock, 0, LC_DEVICE_BLOCK_SIZE);
        i++;
    }

    // cache data initialization
    cdata.hits =0;
    cdata.misses =0;
    cdata.numaccess =0;
    cdata.currentLRU = 0;
    cdata.currentLRUage = 0;
    cdata.bytesused = 0;
    cdata.numitem =0;

    // global var inaitialization
    cachesize = 0;
    maxblock = maxblocks;


    /* Return successfully */
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_closecache
// Description  : Clean up the cache when program is closing
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int lcloud_closecache( void ) {
    int i=0;
    logMessage(LOG_INFO_LEVEL, "Closed cmpsc311 cache, deleting %d items", LC_CACHE_MAXBLOCKS);
    logMessage(LOG_INFO_LEVEL, "Cache hits       [%d]", cdata.hits);
    logMessage(LOG_INFO_LEVEL, "Cache misses     [%d]", cdata.misses);
    logMessage(LOG_INFO_LEVEL, "Cache efficiency [%0.2f%%]", (float)cdata.hits/(float)cdata.numaccess);

    // clean up
    while(i<maxblock){
        cacheinfo[i].did = -1;
        cacheinfo[i].sec = -1;
        cacheinfo[i].blk = -1;
        cacheinfo[i].howold = 0;
        memset(cacheinfo[i].cacheblock, 0, LC_DEVICE_BLOCK_SIZE);
        i++;
    }

    //free
    free(cacheinfo);


    /* Return successfully */
    return( 0 );
}