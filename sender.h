#ifndef __SENDER_H__
#define __SENDER_H__

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


void init_sender(Sender *, int);
void * run_sender(void *);
void ll_split_head(LLnode **, int);
int is_valid_sender(Sender *, int, int);
int is_next_ack(Sender *, int, int);
int send_q_size(Sender *, int);
#endif
