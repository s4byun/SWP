#include "receiver.h"

void init_receiver(Receiver * receiver,
                   int id)
{
    receiver->recv_id = id;
    receiver->input_framelist_head = NULL;
    pthread_cond_init(&receiver->buffer_cv, NULL);
    pthread_mutex_init(&receiver->buffer_mutex, NULL);
    receiver->NFE = 0;
    int i;
    for(i=0; i<WS; i++)
    {
        receiver->recv_q[i] = NULL;
    }
}


void handle_incoming_msgs(Receiver * receiver,
                          LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling incoming frames
    //    1) Dequeue the Frame from the sender->input_framelist_head
    //    2) Convert the char * buffer to a Frame data type
    //    3) Check whether the frame is corrupted
    //    4) Check whether the frame is for this receiver
    //    5) Do sliding window protocol for sender/receiver pair

    int incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
    while (incoming_msgs_length > 0 && recv_q_size(receiver) < 8)
    {
        //Pop a node off the front of the link list and update the count
        LLnode * ll_inmsg_node = ll_pop_node(&receiver->input_framelist_head);
        incoming_msgs_length = ll_get_length(receiver->input_framelist_head);


        char * raw_char_buf = (char *) ll_inmsg_node->value;
        Frame * inframe = convert_char_to_frame(raw_char_buf);

        char crc = crc8(raw_char_buf, MAX_FRAME_SIZE);

        //Checks if message is corrupted
        if(crc == inframe->crc)
        {
            //Checks if message is for this receiver
            if(atoi(inframe->receiver_addr) == receiver->recv_id) 
            {
                //fprintf(stderr, "MESSAGE %s RECEIVED\n", inframe->data);
                int frame_num = (int) inframe->seqnum;

                //Checks if message is already received
                if(is_valid(receiver, frame_num))
                {
                    //Checks if message is in order
                    if(receiver->NFE == frame_num)
                    {
                        receiver->NFE = (receiver->NFE == 255) ? 0 : receiver->NFE + 1;
                        printf("<RECV_%d>:[%s]\n", receiver->recv_id, inframe->data);
                        Frame * next_frame;
                        while((next_frame = find_frame_in_buffer(receiver, receiver->NFE)) != NULL)
                        {
                            printf("<RECV_%d>:[%s]\n", receiver->recv_id, next_frame->data);
                            free(next_frame);
                            receiver->NFE = (receiver->NFE == 255) ? 0 : receiver->NFE + 1;
                        }
                    }
                    //Checks if out of order message is already in buffer
                    else if(is_frame_in_buffer(receiver, frame_num) == 0)
                    {
                        //fprintf(stderr, "MESSAGE %s STORED\n", inframe->data);
                        insert_frame(receiver, inframe);
                    }
                }
            } 

            // Send ACK
            char * ack = convert_frame_to_char(inframe);
            ll_append_node(outgoing_frames_head_ptr, ack);
        }

        //Free raw_char_buf
        free(raw_char_buf);
        free(ll_inmsg_node);
    }
}

int is_valid(Receiver * receiver, int frame_num)
{
    if(receiver->NFE + WS - 1 > MAX_SEQ_NUM)
    {
        if(frame_num >= receiver->NFE || frame_num <= receiver->NFE + MAX_SEQ_NUM - 2)
        {
            return 1;
        }
    }
    else if(frame_num >= receiver->NFE && frame_num <= receiver->NFE + WS - 1)
        return 1;

    return 0;
}

void insert_frame(Receiver * receiver, Frame * frame)
{
    int i;
    for(i=0; i<WS; i++)
    {
        if(receiver->recv_q[i] == NULL)
        {
            receiver->recv_q[i] = frame;
            return;
        }
    }
}

Frame* find_frame_in_buffer(Receiver * receiver, int check_num)
{
    int i;
    for(i=0; i<WS; i++)
    {
        Frame * curr_frame = receiver->recv_q[i];
        if(curr_frame != NULL)
        {
            if((int) curr_frame->seqnum == check_num)
            {
                receiver->recv_q[i] = NULL;
                return curr_frame;
            }
        }
    }

    return NULL;
}

int is_frame_in_buffer(Receiver * receiver, int check_num)
{
    int i;
    for(i=0; i<WS; i++)
    {
        Frame * curr_frame = receiver->recv_q[i];
        if(curr_frame != NULL)
        {
            if((int) curr_frame->seqnum == check_num)
                return 1;
        }
    }

    return 0;
}

int recv_q_size(Receiver * receiver)
{
    int i, count = 0;
    for(i=0; i<WS; i++)
    {
        if(receiver->recv_q[i] != NULL)
            count++;
    }
    return count;
}

void * run_receiver(void * input_receiver)
{    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Receiver * receiver = (Receiver *) input_receiver;
    LLnode * outgoing_frames_head;


    //This incomplete receiver thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up if there is nothing in the incoming queue(s)
    //2. Grab the mutex protecting the input_msg queue
    //3. Dequeues messages from the input_msg queue and prints them
    //4. Releases the lock
    //5. Sends out any outgoing messages

    
    while(1)
    {    
        //NOTE: Add outgoing messages to the outgoing_frames_head pointer
        outgoing_frames_head = NULL;
        gettimeofday(&curr_timeval, 
                     NULL);

        //Either timeout or get woken up because you've received a datagram
        //NOTE: You don't really need to do anything here, but it might be useful for debugging purposes to have the receivers periodically wakeup and print info
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;
        time_spec.tv_sec += WAIT_SEC_TIME;
        time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        if (time_spec.tv_nsec >= 1000000000)
        {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&receiver->buffer_mutex);

        //Check whether anything arrived
        int incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
        if (incoming_msgs_length == 0)
        {
            //Nothing has arrived, do a timed wait on the condition variable (which releases the mutex). Again, you don't really need to do the timed wait.
            //A signal on the condition variable will wake up the thread and reacquire the lock
            pthread_cond_timedwait(&receiver->buffer_cv, 
                                   &receiver->buffer_mutex,
                                   &time_spec);
        }

        handle_incoming_msgs(receiver,
                             &outgoing_frames_head);

        pthread_mutex_unlock(&receiver->buffer_mutex);
        
        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames user has appended to the outgoing_frames list
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        while(ll_outgoing_frame_length > 0)
        {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *) ll_outframe_node->value;
            
            //The following function frees the memory for the char_buf object
            send_msg_to_senders(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);

}
