#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"


// Global variable to track the mount status
int isMounted = 0; // 0 indicates unmounted, 1 indicates mounted

// Helper function to construct the operation command
int helperFunction(uint32_t DiskID, uint32_t BlockID, uint32_t Command, uint32_t Reserved) {
    // Bitwise left shift to construct the operation command
    BlockID = BlockID << 4;
    Command = Command << 12;
    Reserved = Reserved << 20;
    return DiskID | BlockID | Command | Reserved;
}

// Function to change the mount status
void updateMountStatus(int updated_status) {
    isMounted = updated_status;
}

// Function to perform JBOD operation
int performOperations(jbod_cmd_t command, uint32_t disk, uint32_t block, uint8_t* data) {
    int result = jbod_client_operation(helperFunction(disk, block, command, 0), data);
    return (result == -1) ? -1 : 1;
}


// Function to mount the JBOD disks
int mdadm_mount(void) {
    

    if (isMounted == 1) return -1; // Already mounted, return -1 to indicate failure

    jbod_cmd_t command = JBOD_MOUNT; // Set the JBOD command to MOUNT
    int operation_result = performOperations(command, 0, 0, NULL); // Execute JBOD operation

    updateMountStatus(1); // Update the mount status

    return operation_result;
}

// Function to unmount the JBOD disks
int mdadm_unmount(void) {
    
    if (isMounted == 0) return -1; // Already unmounted, return -1 to indicate failure

    jbod_cmd_t command = JBOD_UNMOUNT; // Set the JBOD command to UNMOUNT
    int operation_result = performOperations(command, 0, 0, NULL); // Execute JBOD operation

    updateMountStatus(0); // Update the mount status

    return operation_result;
}

int mdadm_write_permission(void){
    jbod_cmd_t command = JBOD_WRITE_PERMISSION;
    return performOperations(command, 0, 0, NULL);
	
}
 


int mdadm_revoke_write_permission(void){
    jbod_cmd_t command = JBOD_REVOKE_WRITE_PERMISSION;
    return performOperations(command, 0, 0, NULL);
	
}






uint32_t readBlockAndCopy(uint32_t diskNumber, uint32_t currentBlock, uint32_t offsetWithinBlock, uint8_t *readBuf, uint32_t bytesLeft) {
    uint32_t bytesToCopy;

    if (cache_enabled() && cache_lookup(diskNumber, currentBlock, readBuf) == 1) {
        bytesToCopy = JBOD_BLOCK_SIZE - offsetWithinBlock;
        bytesToCopy = (bytesToCopy > bytesLeft) ? bytesLeft : bytesToCopy;
    } else {
        jbod_client_operation(helperFunction(diskNumber, 0, JBOD_SEEK_TO_DISK, 0), NULL);
        jbod_client_operation(helperFunction(0, currentBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);

        static uint8_t temp[JBOD_BLOCK_SIZE];
        jbod_client_operation(helperFunction(0, 0, JBOD_READ_BLOCK, 0), temp);

        bytesToCopy = JBOD_BLOCK_SIZE - offsetWithinBlock;
        bytesToCopy = (bytesToCopy > bytesLeft) ? bytesLeft : bytesToCopy;
        memcpy(readBuf, temp + offsetWithinBlock, bytesToCopy);

        if (cache_enabled()) {
            cache_insert(diskNumber, currentBlock, temp);
        }
    }

    return bytesToCopy;
}

uint32_t performReadOperation(uint32_t startAddr, uint32_t readLen, uint8_t *readBuf) {
    uint32_t bytesLeft = readLen;
    uint32_t offsetBuffer = 0;

    while (bytesLeft > 0) {
        uint32_t diskNumber = startAddr / (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE);
        uint32_t currentBlock = (startAddr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK;
        uint32_t offsetWithinBlock = startAddr % JBOD_BLOCK_SIZE;

        // Read from JBOD or cache (handled in readBlockAndCopy)
        uint32_t bytesCopied = readBlockAndCopy(diskNumber, currentBlock, offsetWithinBlock, readBuf + offsetBuffer, bytesLeft);
        bytesLeft -= bytesCopied;
        offsetBuffer += bytesCopied;
        startAddr += bytesCopied;
    }

    return readLen;
}

int mdadm_read(uint32_t startAddr, uint32_t readLen, uint8_t *readBuf) {
    if (isMounted == 0 || readLen > 1024 || startAddr < 0 || startAddr + readLen > 1048576 || (readBuf == NULL && readLen != 0)) {
        return -1;
    }

    return performReadOperation(startAddr, readLen, readBuf);
}
 


int readAndModifyBlock(uint32_t diskNumber, uint32_t currentBlock, uint32_t offsetWithinBlock, const uint8_t *writeBuf, uint32_t bytesLeft) {
    uint8_t temp[JBOD_BLOCK_SIZE];
    jbod_client_operation(helperFunction(diskNumber, 0, JBOD_SEEK_TO_DISK, 0), NULL);
    jbod_client_operation(helperFunction(0, currentBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
    jbod_client_operation(helperFunction(0, 0, JBOD_READ_BLOCK, 0), temp);

    uint32_t bytesToCopy = JBOD_BLOCK_SIZE - offsetWithinBlock;
    bytesToCopy = (bytesToCopy > bytesLeft) ? bytesLeft : bytesToCopy;
    memcpy(temp + offsetWithinBlock, writeBuf, bytesToCopy);

    jbod_client_operation(helperFunction(0, currentBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
    jbod_client_operation(helperFunction(0, 0, JBOD_WRITE_BLOCK, 0), temp);

    return bytesToCopy;
}

// Sub-function to perform the overall write operation
int performWriteOperation(uint32_t startAddr, uint32_t writeLen, const uint8_t *writeBuf) {
    uint32_t bytesLeft = writeLen;
    uint32_t offsetBuffer = 0;

    while (bytesLeft > 0) {
        uint32_t diskNumber = startAddr / (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE);
        uint32_t currentBlock = (startAddr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK;
        uint32_t offsetWithinBlock = startAddr % JBOD_BLOCK_SIZE;

        uint32_t bytesCopied = readAndModifyBlock(diskNumber, currentBlock, offsetWithinBlock, writeBuf + offsetBuffer, bytesLeft);

        bytesLeft -= bytesCopied;
        offsetBuffer += bytesCopied;

        offsetWithinBlock = 0;
        currentBlock++;
        startAddr += bytesCopied;

        if (currentBlock == JBOD_NUM_BLOCKS_PER_DISK) {
            break;
        }
    }

    while (bytesLeft > 0) {
        uint32_t diskNumber = startAddr / (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE);
        uint32_t currentBlock = (startAddr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK;

        uint32_t bytesCopied = readAndModifyBlock(diskNumber, currentBlock, 0, writeBuf + offsetBuffer, bytesLeft);

        bytesLeft -= bytesCopied;
        offsetBuffer += bytesCopied;

        currentBlock++;
        startAddr += bytesCopied;
    }
    return writeLen;
}


int mdadm_write(uint32_t startAddr, uint32_t writeLen, const uint8_t *writeBuf) {
    if (isMounted == 0 || writeLen > 1024 || startAddr < 0 || startAddr + writeLen > 1048576 || (writeBuf == NULL && writeLen != 0)) {
        return -1;
    }

    // Check if the cache is enabled
    if (cache_enabled()) {
        // Try to find the block in the cache
        uint8_t cacheBuf[JBOD_BLOCK_SIZE];
        if (cache_lookup(startAddr / JBOD_BLOCK_SIZE, startAddr % JBOD_BLOCK_SIZE, cacheBuf) == 1) {
            // If found, update the cache and return
            cache_update(startAddr / JBOD_BLOCK_SIZE, startAddr % JBOD_BLOCK_SIZE, writeBuf);
            return performWriteOperation(startAddr, writeLen, writeBuf);
        }
    }

    // If not found in cache or caching is disabled, perform the actual write operation
    int result = performWriteOperation(startAddr, writeLen, writeBuf);

    // If the write operation was successful, insert the block into the cache
    if (result != -1 && cache_enabled()) {
        cache_insert(startAddr / JBOD_BLOCK_SIZE, startAddr % JBOD_BLOCK_SIZE, writeBuf);
    }

    return result;
} 
