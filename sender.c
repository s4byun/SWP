#include "sender.h"

void init_sender(Sender * sender, int id)
{
    //TODO: You should fill in this function as necessary
    sender->send_id = id;
    sender->input_cmdlist_head = NULL;
    sender->input_framelist_head = NULL;
    sender->send_q_head = NULL; 
    pthread_cond_init(&sender->buffer_cv, NULL);
    pthread_mutex_init(&sender->buffer_mutex, NULL);
    sender->seqnum = 0;
    sender->LFS = -1;
    sender->LAR = -1;
}

struct timeval * sender_get_next_expiring_timeval(Sender * sender)
{
    //TODO: You should fill in this function so that it returns the next timeout that should occur

    struct timeval* tv = NULL;
    LLnode * curr_node = sender->send_q_head;
    int i;
    for(i=0; i < send_q_size(sender); i++)
    {
        send_Q * curr = (send_Q *) curr_node->value;
        curr_node = curr_node->next;
        if(curr->frame_timeout == NULL)
            continue;

        if(tv == NULL || timeval_usecdiff(tv, curr->frame_timeout) < 0)
            tv = curr->frame_timeout;
    }
    
    return tv;
}


void handle_incoming_acks(Sender * sender,
                          LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling incoming ACKs
    //    1) Dequeue the ACK from the sender->input_framelist_head
    //    2) Convert the char * buffer to a Frame data type
    //    3) Check whether the frame is corrupted
    //    4) Check whether the frame is for this sender
    //    5) Do sliding window protocol for sender/receiver pair   

    int incoming_msgs_length = ll_get_length(sender->input_framelist_head);
    while(incoming_msgs_length > 0)
    {
        LLnode * ll_inmsg_node = ll_pop_node(&sender->input_framelist_head);
        incoming_msgs_length = ll_get_length(sender->input_framelist_head);

        char * raw_char_buf = (char *) ll_inmsg_node->value;
        Frame * inframe = convert_char_to_frame(raw_char_buf);
        ll_destroy_node(ll_inmsg_node);

        if(crc8(raw_char_buf, MAX_FRAME_SIZE))
        {
            if(atoi(inframe->sender_addr) == sender->send_id)
            {
                if(is_next_ack(sender, (int) inframe->seqnum))
                {
                    //fprintf(stderr, "ACK %d received\n", (int) inframe->seqnum % WS);
                    sender->LAR = (int) inframe->seqnum;

                    LLnode * acked_node = ll_pop_node(&sender->send_q_head);
                    ll_destroy_sendQ(acked_node);
                }
            }
        }
        
        free(inframe);
    }
    int i;
    for(i=0; i < send_q_size(sender); i++)
    {
        //fprintf(stderr, "ACK %d LOST!!!\n", i);
    }
}


void handle_input_cmds(Sender * sender,
                       LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling input cmd
    //    1) Dequeue the Cmd from sender->input_cmdlist_head
    //    2) Convert to Frame
    //    3) Set up the frame according to the sliding window protocol
    //    4) Compute CRC and add CRC to Frame

    int input_cmd_length = ll_get_length(sender->input_cmdlist_head);

    //Recheck the command queue length to see if stdin_thread dumped a command on us
    input_cmd_length = ll_get_length(sender->input_cmdlist_head);
    while (input_cmd_length > 0 && send_q_size(sender) < WS)
    {
        //Pop a node off and update the input_cmd_length
        LLnode * ll_input_cmd_node = ll_pop_node(&sender->input_cmdlist_head);
        input_cmd_length = ll_get_length(sender->input_cmdlist_head);

        //Cast to Cmd type and free up the memory for the node
        Cmd * outgoing_cmd = (Cmd *) ll_input_cmd_node->value;
        free(ll_input_cmd_node);
            

        //DUMMY CODE: Add the raw char buf to the outgoing_frames list
        //NOTE: You should not blindly send this message out!
        //      Ask yourself: Is this message actually going to the right receiver (recall that default behavior of send is to broadcast to all receivers)?
        //                    Does the receiver have enough space in in it's input queue to handle this message?
        //                    Were the previous messages sent to this receiver ACTUALLY delivered to the receiver?
        int msg_length = strlen(outgoing_cmd->message);
        if (msg_length > FRAME_PAYLOAD_SIZE - 1)
        {
            ll_append_node(&sender->input_cmdlist_head, outgoing_cmd);
            ll_split_head(&sender->input_cmdlist_head, FRAME_PAYLOAD_SIZE - 1); 
            input_cmd_length = ll_get_length(sender->input_cmdlist_head);
            //fprintf(stderr, "Sending messages of length greater than %d\n", FRAME_PAYLOAD_SIZE);
            continue;
        }
        else
        {
            //This is probably ONLY one step you want
            Frame * outgoing_frame = (Frame *) malloc (sizeof(Frame));

            //Convert uint16_t to string
            char* src = malloc(MAC_ADDR_SIZE);
            char* dst = malloc(MAC_ADDR_SIZE);
            sprintf(src, "%u", outgoing_cmd->src_id);
            sprintf(dst, "%u", outgoing_cmd->dst_id);

            strncpy(outgoing_frame->receiver_addr, dst, MAC_ADDR_SIZE);
            strncpy(outgoing_frame->sender_addr, src, MAC_ADDR_SIZE);
            strncpy(outgoing_frame->data, outgoing_cmd->message, FRAME_PAYLOAD_SIZE);

            /*
            while (sender->send_q[sender->seqnum%8].sent_frame != NULL)
                sender->seqnum = (sender->seqnum == 255) ? 0 : sender->seqnum + 1;
            */
            outgoing_frame->seqnum = sender->seqnum;

            char* raw_char_buf = convert_frame_to_char(outgoing_frame); 
            outgoing_frame->crc = crc8(raw_char_buf, MAX_FRAME_SIZE);

            free(raw_char_buf);

            //At this point, we don't need the outgoing_cmd
            free(outgoing_cmd->message);
            free(outgoing_cmd);

            sender->LFS = sender->seqnum;

            //Convert the message to the outgoing_charbuf
            char * outgoing_charbuf = convert_frame_to_char(outgoing_frame);
            ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);
            send_Q * sent_buf = malloc(sizeof(send_Q));
            sent_buf->frame = malloc(sizeof(Frame));
            sent_buf->frame = outgoing_frame;
            sent_buf->frame_timeout = NULL;
            
            ll_append_node(&sender->send_q_head, sent_buf);
            sender->seqnum = (sender->seqnum == 255) ? 0 : sender->seqnum + 1;
        }
    }
}

void handle_timedout_frames(Sender * sender,
                            LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling timed out datagrams
    //    1) Iterate through the sliding window protocol information you maintain for each receiver
    //    2) Locate frames that are timed out and add them to the outgoing frames
    //    3) Update the next timeout field on the outgoing frames

    int i;
    LLnode * curr_node = sender->send_q_head;
    for(i=0; i < send_q_size(sender); i++)
    {
        send_Q * curr = (send_Q *) curr_node->value;
        Frame * sent_frame = curr->frame;
        struct timeval * tv = curr->frame_timeout;
        struct timeval now;
        gettimeofday(&now, NULL);
        if(tv != NULL)
        {
            if(timeval_usecdiff(tv, &now) > 0)
            {
                char * outframe_char_buf = convert_frame_to_char(sent_frame);
                ll_append_node(outgoing_frames_head_ptr, outframe_char_buf);
                calculate_timeout(curr->frame_timeout);
            }
        }
        if(tv == NULL)
        {
            curr->frame_timeout = (struct timeval *) malloc(sizeof(struct timeval));
            calculate_timeout(curr->frame_timeout);
        }
    }
}

void ll_split_head(LLnode  ** head_ptr, int payload_size)
{
    if(head_ptr == NULL || *head_ptr == NULL)
        return;

    //Get the message from the head of the linked list
    LLnode * head = *head_ptr;
    Cmd* head_cmd = (Cmd*) head->value;
    char* msg = head_cmd->message;

    //Do not need to split
    if(strlen(msg) < payload_size)
        return;

    int i;
    for(i = payload_size; i < strlen(msg); i+= payload_size)
    {
        Cmd* next_cmd = (Cmd*) malloc(sizeof(Cmd));
        next_cmd->message = (char *) malloc((payload_size+1)*sizeof(char));
        memset(next_cmd->message, 0, (payload_size+1)*sizeof(char));
        strncpy(next_cmd->message, msg + i, payload_size*sizeof(char));
        next_cmd->src_id = head_cmd->src_id;
        next_cmd->dst_id = head_cmd->dst_id;
        ll_append_node(head_ptr, next_cmd);
    }
    head_cmd->message[payload_size] = '\0';
}

int send_q_size(Sender * sender)
{
    if(sender->LFS == sender->LAR)
        return 0;
    else if(sender->LFS > sender->LAR)
    {
        return sender->LFS - sender->LAR; 
    }
    else
    {
        return sender->LFS + MAX_SEQ_NUM - sender->LAR; 
    }
}

int is_next_ack(Sender* sender, int ack)
{
    int next_ack = (sender->LAR == 255) ? 0 : sender->LAR + 1;
    if(ack == next_ack)
        return 1;
    else
        return 0;
}

void * run_sender(void * input_sender)
{    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Sender * sender = (Sender *) input_sender;    
    LLnode * outgoing_frames_head;
    struct timeval * expiring_timeval;
    long sleep_usec_time, sleep_sec_time;
    
    //This incomplete sender thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up
    //2. Grab the mutex protecting the input_cmd/inframe queues
    //3. Dequeues messages from the input queue and adds them to the outgoing_frames list
    //4. Releases the lock
    //5. Sends out the messages

    while(1)
    {    
        outgoing_frames_head = NULL;

        //Get the current time
        gettimeofday(&curr_timeval, 
                     NULL);

        //time_spec is a data structure used to specify when the thread should wake up
        //The time is specified as an ABSOLUTE (meaning, conceptually, you specify 9/23/2010 @ 1pm, wakeup)
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;

        //Check for the next event we should handle
        expiring_timeval = sender_get_next_expiring_timeval(sender);

        //Perform full on timeout
        if (expiring_timeval == NULL)
        {
            time_spec.tv_sec += WAIT_SEC_TIME;
            time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        }
        else
        {
            //Take the difference between the next event and the current time
            sleep_usec_time = timeval_usecdiff(&curr_timeval,
                                               expiring_timeval);

            //Sleep if the difference is positive
            if (sleep_usec_time > 0)
            {
                sleep_sec_time = sleep_usec_time/1000000;
                sleep_usec_time = sleep_usec_time % 1000000;   
                time_spec.tv_sec += sleep_sec_time;
                time_spec.tv_nsec += sleep_usec_time*1000;
            }   
        }

        //Check to make sure we didn't "overflow" the nanosecond field
        if (time_spec.tv_nsec >= 1000000000)
        {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        
        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames or input commands should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&sender->buffer_mutex);

        //Check whether anything has arrived
        int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(sender->input_framelist_head);
        
        //Nothing (cmd nor incoming frame) has arrived, so do a timed wait on the sender's condition variable (releases lock)
        //A signal on the condition variable will wakeup the thread and reaquire the lock
        if (input_cmd_length == 0 &&
            inframe_queue_length == 0)
        {
            
            pthread_cond_timedwait(&sender->buffer_cv, 
                                   &sender->buffer_mutex,
                                   &time_spec);
        }
        //Implement this
        handle_incoming_acks(sender,
                             &outgoing_frames_head);

        //Implement this
        handle_input_cmds(sender,
                          &outgoing_frames_head);

        pthread_mutex_unlock(&sender->buffer_mutex);


        //Implement this
        handle_timedout_frames(sender,
                               &outgoing_frames_head);

        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        
        while(ll_outgoing_frame_length > 0)
        {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *)  ll_outframe_node->value;

            //Don't worry about freeing the char_buf, the following function does that
            send_msg_to_receivers(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);
    return 0;
}


