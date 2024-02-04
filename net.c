#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"


int cli_sd = -1;


static bool nread(int fd, int len, uint8_t *buf) {
    ssize_t bytes_read = 0;
    while (bytes_read < len) {
        ssize_t nth_read = read(fd, &buf[bytes_read], len - bytes_read);
        if (nth_read == -1) {
            return false; // Error in reading the bytes
        }
        if (nth_read == 0) {
            return false; // No data to read
        }
        bytes_read += nth_read;
    }
    return true;
}



static bool nwrite(int fd, int len, uint8_t *buf) {
    ssize_t bytes_written = 0;
    while (bytes_written < len) {
        ssize_t nth_write = write(fd, &buf[bytes_written], len - bytes_written);
        if (nth_write == -1) {
          // Error in writing
            return false; 
        }
        bytes_written += nth_write;
    }
    return true;
}

/* attempts to receive a packet from fd; returns true on success and false on failure */
static bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block){

  // Creating the myBuf to copy into
  uint8_t myBuf[HEADER_LEN];
  // uint16_t length;

  if(nread(fd, HEADER_LEN, myBuf) == false)
  {
    return false;
  }

  

  *op = ntohl(*op);

  memcpy(ret, myBuf + 4 , sizeof(*ret));
  
  
  if ((*ret & 2) == 0) {
    return true;
}

  if (nread(fd, JBOD_BLOCK_SIZE, block) == 0) {
    return false;
}

  return true;
}


static bool send_packet(int sd, uint32_t op, uint8_t *block) {
    uint8_t buffer_send[HEADER_LEN + JBOD_BLOCK_SIZE];
    uint16_t len = HEADER_LEN;
    uint32_t network_op = htonl(op);
    int temp_offset = 0;

    
    memcpy(buffer_send + temp_offset, &network_op, sizeof(network_op));
    temp_offset += sizeof(network_op);

    // Determine the command and adjust length and buffer accordingly
    uint32_t command = (op >> 12) & 0x3F;
    if (command == JBOD_WRITE_BLOCK) {
        len += JBOD_BLOCK_SIZE; // Adjust length for write operation
        buffer_send[temp_offset] = 2; // Setting appropriate info value for write operation
        memcpy(buffer_send + HEADER_LEN, block, JBOD_BLOCK_SIZE); // Copying the block if write operation
    } else {
        buffer_send[temp_offset] = 0; // Setting appropriate info value for non-write operation
    }

    
    /*printf("Sent packet with op: %u\n", ntohl(op));
    if (command == JBOD_WRITE_BLOCK)
    {
        printf("Sent block data:\n");
        for (int i = 0; i < JBOD_BLOCK_SIZE; i++)
        {
            //printf("%u ", block[i]);
        }
        printf("\n"); 
    } */

    
    return nwrite(sd, len, buffer_send);
}



bool jbod_connect(const char *ip, uint16_t port)
{



  //printf("is it connecting\n " );

  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(JBOD_PORT);

  if (inet_aton(ip, &(caddr.sin_addr)) == 0 || 
  (cli_sd = socket(PF_INET, SOCK_STREAM, 0)) == -1 || 
  connect(cli_sd, (struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
    return false;
}


  //printf("is it connecting\n " );

  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void)
{
  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server and receives and processes the
response. */
int jbod_client_operation(uint32_t op, uint8_t *block) {

  uint32_t received_op_code = 0;
  uint8_t response_status = 0;
    if (cli_sd == -1 || 
    send_packet(cli_sd, op, block) == 0 || 
    recv_packet(cli_sd, &received_op_code, &response_status, block) == 0 || 
    op != received_op_code || 
    (response_status %= 2) != 0) {
    return -1;
}


    // Return the response status (expected to be 0 at this point)
    return response_status;
}