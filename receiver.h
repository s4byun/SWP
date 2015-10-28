#ifndef __RECEIVER_H__
#define __RECEIVER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <math.h>
#include <sys/time.h>
#include "common.h"
#include "util.h"
#include "communicate.h"

void init_receiver(Receiver *, int);
void insert_frame(Receiver *, Frame *);
Frame* find_frame_in_buffer(Receiver *, int);
int is_valid(Receiver *, int);
int recv_q_size(Receiver *);
int is_frame_in_buffer(Receiver *, int);
void * run_receiver(void *);
#endif
