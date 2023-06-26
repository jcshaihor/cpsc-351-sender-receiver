#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "msg.h" /* For the message struct */

/* The size of the shared memory segment */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The ids for the shared memory segment and the message queue */
int shmid, msqid;

/* The pointer to the shared memory */
void* sharedMemPtr;

void init(int& shmid, int& msqid, void*& sharedMemPtr)
{
    key_t key = ftok("keyfile.txt", 'a'); /* generates key */
    if (key == -1)
    {
        perror("ftok");
        exit(-1);
    }

    shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE, IPC_CREAT | S_IRUSR | S_IWUSR); /* gets id of the shared memory segment */
    if (shmid == -1)
    {
        perror("shmget");
        exit(-1);
    }

    sharedMemPtr = shmat(shmid, NULL, 0); /* attaches to shared memory */
    if (sharedMemPtr == (void*)-1)
    {
        perror("shmat");
        exit(-1);
    }

    msqid = msgget(key, IPC_CREAT | S_IRUSR | S_IWUSR); /* attaches to the message queue */
    if (msqid == -1)
    {
        perror("msgget");
        exit(-1);
    }
}

void cleanUp(const int& shmid, const int& msqid, void* sharedMemPtr)
{
    /* detaches from shared memory */
    shmdt(sharedMemPtr); 
    /* deallocate shared memory segment */
    shmctl(shmid, IPC_RMID, NULL);
    /* deallocate message queue */
    msgctl(msqid, IPC_RMID, NULL); 
}

unsigned long sendFile(const char* fileName)
{
    /* A buffer to store the message we will send to the receiver. */
    message sndMsg;

    /* A buffer to store the message received from the receiver. */
    ackMessage rcvMsg;

    /* The number of bytes sent */
    unsigned long numBytesSent = 0;

    /* Open the file */
    FILE* fp = fopen(fileName, "r");

    /* Was the file open? */
    if (!fp)
    {
        perror("fopen");
        exit(-1);
    }

    /* Read the whole file */
    while (!feof(fp))
    {
        /* Read at most SHARED_MEMORY_CHUNK_SIZE from the file and
         * store them in shared memory. fread() will return how many bytes it has
         * actually read. This is important; the last chunk read may be less than
         * SHARED_MEMORY_CHUNK_SIZE.
         */
        if ((sndMsg.size = fread(sharedMemPtr, sizeof(char), SHARED_MEMORY_CHUNK_SIZE, fp)) < 0)
        {
            perror("fread");
            exit(-1);
        }
        /* Count the number of bytes sent */
        numBytesSent += sndMsg.size; 

        sndMsg.mtype = SENDER_DATA_TYPE;
        /* Send a message to the receiver telling him that the data is ready
         * to be read (message of type SENDER_DATA_TYPE).
         */
        msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0);

        /* Wait until the receiver sends us a message of type RECV_DONE_TYPE telling us
         * that he finished saving a chunk of memory.
         */
        msgrcv(msqid, &rcvMsg, sizeof(ackMessage) - sizeof(long), RECV_DONE_TYPE, 0);
    }

    /* Inform the receiver that we have finished sending the file by sending a message
     * of type SENDER_DATA_TYPE with size field set to 0.
     */
    sndMsg.mtype = SENDER_DATA_TYPE;
    sndMsg.size = 0;
    /* Send the message to the receiver */
    msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0);

    /* Close the file */
    fclose(fp);

    return numBytesSent;
}

/**
 * Used to send the name of the file to the receiver
 * @param fileName - the name of the file to send
 */
void sendFileName(const char* fileName)
{
    /* Get the length of the file name */
    int fileNameSize = strlen(fileName);

    /* Make sure the file name does not exceed the maximum buffer size in the fileNameMsg struct.
     * If it exceeds, terminate with an error.
     */
    if (fileNameSize > MAX_FILE_NAME_SIZE)
    {
        fprintf(stderr, "Name exceeds maximum buffer size.");
        exit(-1);
    }

    /* Create an instance of the struct representing the message containing the name of the file */
    fileNameMsg fNameMsg;

    /* Set the message type as FILE_NAME_TRANSFER_TYPE */
    fNameMsg.mtype = FILE_NAME_TRANSFER_TYPE;

    /* Set the file name in the message */
    strcpy(fNameMsg.fileName, fileName);

    /* Send the message using msgsnd */
    msgsnd(msqid, &fNameMsg, sizeof(fileNameMsg) - sizeof(long), 0);
}

int main(int argc, char** argv)
{
    /* Check the command line arguments */
    if (argc < 2)
    {
        fprintf(stderr, "USAGE: %s <FILE NAME>\n", argv[0]);
        exit(-1);
    }

    /* Connect to shared memory and the message queue */
    init(shmid, msqid, sharedMemPtr);

    /* Send the name of the file */
    sendFileName(argv[1]);

    /* Send the file */
    fprintf(stderr, "The number of bytes sent is %lu\n", sendFile(argv[1]));

    /* Cleanup */
    cleanUp(shmid, msqid, sharedMemPtr);

    return 0;
}