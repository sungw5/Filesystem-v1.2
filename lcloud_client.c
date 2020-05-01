////////////////////////////////////////////////////////////////////////////////
//
//  File          : lcloud_client.c
//  Description   : This is the client side of the Lion Clound network
//                  communication protocol.
//
//  Author        : Sung Woo Oh
//  Last Modified : Sat 28 Mar 2020 09:43:05 AM EDT
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <cmpsc311_util.h>  // for hton64ll() and ntoh64ll() functions

// Project Include Files
#include <lcloud_network.h>
#include <cmpsc311_log.h>


static LCloudRegisterFrame b0, b1, c0, c1, c2, d0, d1; // global variable for registers

struct sockaddr_in caddr;
int sockfd; 
int socket_handle = -1;


////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_lcloud_registers
// Description  : unpack the registers created with previous function(resp)
//

uint64_t extract_network_registers(LCloudRegisterFrame resp){
    b0 = (resp >> 60) & 0xf;                    // 0 - sending / 1 - responding
    b1 = (resp >> 56) & 0xf;                    // 0 - sending to device / 1 - success from device
    c0 = (resp >> 48) & 0xff;   /*****  OPCODE : 0 - POWERON / 1 - DEVPROBE / 2 - DEVINIT / 3 - BLOCK_XFER  *****/
    c1 = (resp >> 40) & 0xff;                   // DID
    c2 = (resp >> 32) & 0xff;   /*****  LC_XFER_READ - 0 / LC_XFER_WRITE - 1  *****/
    d0 = (resp >> 16) & 0xffff;                 // sec
    d1 = (resp & 0xffff);                       // blk
    
    return c0; // return operation code
}

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_lcloud_bus_request
// Description  : This the client regstateeration that sends a request to the 
//                lion client server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed

LCloudRegisterFrame client_lcloud_bus_request( LCloudRegisterFrame reg, void *buf ) {
    const char *ip = LCLOUD_DEFAULT_IP;
    // Set up the address information
    caddr.sin_family = AF_INET;
    caddr.sin_port = htons(LCLOUD_DEFAULT_PORT); //htons converts integers to be in network byte order (always big Endian) // literally means host to network
    socklen_t addrlen = sizeof(caddr);

    
    
    // If there isn't an open connection already created, three things need 
    if(socket_handle == -1){
        // (a) Setup the address
        if(inet_aton(ip, &caddr.sin_addr) == 0){
            logMessage(LOG_ERROR_LEVEL, "Error on address setup\n");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "IPv4: %s/%d\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
        

        // (b) Create the socket  - socket() 
        sockfd = socket(PF_INET, SOCK_STREAM, 0);
        if(sockfd == -1){
            logMessage(LOG_ERROR_LEVEL, "Error on socket creation [%s]\n", strerror(errno));
            return -1;
        }

        // (c) Create the connection  - connect()
        if(connect(sockfd, (const struct sockaddr *)&caddr, addrlen) == -1){
            logMessage(LOG_ERROR_LEVEL, "Error on connection\n");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "Successfully made a connection...");
        socket_handle = 1;
    }

    LCloudRegisterFrame opcode = extract_network_registers(reg); 

    // read( fd, buf, len ) - to receive data FROM a remote process over the network
    // write( fd, buf, len ) - to send data TO a remote process over the network
    // fd - file descriptor / buf - array of bytes to write/read / len - number of bytes to write/read
    // both returns length of bytes read/written
    

    // There are four cases to consider when extracting this opcode.
        
        // CASE 1: read operation (look at the c0 and c2 fields)
        // SEND: (reg) <- Network format : send the register reg to the network
        // after converting the register to 'network format'.
        //
        // RECEIVE: (reg) -> Host format
        //          256-byte block (Data read from that frame)

    LCloudRegisterFrame networkbyte = htonll64(reg); // convert reg to 'network format' (host to network)
    //LCloudRegisterFrame hostbyte = ntohll64(networkbyte);

    if(opcode == LC_BLOCK_XFER && c2 == LC_XFER_READ){
        logMessage(LOG_INFO_LEVEL, "READ connection start...");

        if(write(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to send the register reg to the network (READ FAIL)");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "Sent the register(%d)", ntohll64(networkbyte), caddr.sin_port);

        if(read(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to receive the register reg (READ FAIL)");
            return -1;
        }

        if(read(sockfd, buf, LC_DEVICE_BLOCK_SIZE) != LC_DEVICE_BLOCK_SIZE){
            logMessage(LOG_ERROR_LEVEL, "Failed to read from the block (READ FAIL)");
            return -1;
        }

        // When successfully completed read operation, return encoded response
        return ntohll64(networkbyte);
        
    }

        // CASE 2: write operation (look at the c0 and c2 fields)
        // SEND: (reg) <- Network format : send the register reg to the network 
        // after converting the register to 'network format'.
        //       buf 256-byte block (Data to write to that frame)
        //
        // RECEIVE: (reg) -> Host format

    else if(opcode == LC_BLOCK_XFER && c2 == LC_XFER_WRITE){
        logMessage(LOG_INFO_LEVEL, "WRITE connection start...");

        if(write(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to send the register reg to the network (WRITE FAIL)");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "Sent the register(%d)", ntohll64(networkbyte));

        if(write(sockfd, buf, LC_DEVICE_BLOCK_SIZE) != LC_DEVICE_BLOCK_SIZE){
            logMessage(LOG_ERROR_LEVEL, "Failed to write to the block (WRITE FAIL)");
            return -1;
        }

        if(read(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to receive the register reg (WRITE FAIL)");
            return -1;
        }

        return ntohll64(networkbyte);

    }

        // CASE 3: power off operation
        // SEND: (reg) <- Network format : send the register reg to the network 
        // after converting the register to 'network format'.
        //
        // RECEIVE: (reg) -> Host format
        //
        // Close the socket when finished : reset socket_handle to initial value of -1.
        // close(socket_handle)
    
    else if(opcode == LC_POWER_OFF){
        logMessage(LOG_INFO_LEVEL, "POWEROFF connection start...");

        if(write(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to send the register reg to the network (POWEROFF FAIL)");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "Sent the register(%d)", ntohll64(networkbyte));

        if(read(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to receive the register reg (POWEROFF FAIL)");
            return -1;
        }
        close(sockfd);
        socket_handle = -1; //to avoid use after close

        return ntohll64(networkbyte);
    }

        // CASE 4: Other operations (probes, ...)
        // SEND: (reg) <- Network format : send the register reg to the network 
        // after converting the register to 'network format'.
        //
        // RECEIVE: (reg) -> Host format

    else{
        logMessage(LOG_INFO_LEVEL, "POWERON/DEVINIT/OTHER connection start...");

        if(write(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to send the register reg to the network (PROBE or OTHER FAIL)");
            return -1;
        }
        logMessage(LOG_INFO_LEVEL, "Sent the register(%d)", ntohll64(networkbyte));
        

        if(read(sockfd, &networkbyte, sizeof(networkbyte)) != sizeof(networkbyte)){
            logMessage(LOG_ERROR_LEVEL, "Failed to receive the register reg (PROBE or OTHER FAIL)");
            return -1;
        }

        return ntohll64(networkbyte);
    }






}

