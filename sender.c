#include "sender.h"

void init_sender(Sender * sender, int id)
{
    sender->send_id = id;
    sender->input_cmdlist_head = NULL;
    sender->input_framelist_head = NULL; 
    pthread_cond_init(&sender->buffer_cv, NULL);
    pthread_mutex_init(&sender->buffer_mutex, NULL);
    sender->send_q_head = malloc(sizeof(send_Q*)*glb_receivers_array_length);
    sender->seqnum = malloc(sizeof(int)*glb_receivers_array_length);
    sender->LFS = malloc(sizeof(int)*glb_receivers_array_length);
    sender->LAR = malloc(sizeof(int)*glb_receivers_array_length);

    int i,j;
    for(i=0; i<glb_receivers_array_length; i++)
    {
        sender->send_q_head[i] = malloc(sizeof(send_Q*)*WS);
        sender->seqnum[i] = 0;
        sender->LFS[i] = -1;
        sender->LAR[i] = -1;
        for(j=0; j<WS; j++)
        {
            sender->send_q_head[i][j] = malloc(sizeof(send_Q));
            sender->send_q_head[i][j]->frame = NULL;
            sender->send_q_head[i][j]->frame_timeout = NULL;
        }
    }
}

struct timeval * sender_get_next_expiring_timeval(Sender * sender)
{
    struct timeval* tv = NULL;
    int i,j;

    //Iterate through the send queue for each receiver state
    for(i=0; i < glb_receivers_array_length; i++)
    {
        send_Q ** curr_node = sender->send_q_head[i];
        for(j=0; j < WS; j++)
        {
            send_Q* curr = curr_node[j];

            if(curr->frame_timeout == NULL)
                continue;

            //Found the next expiring timeval
            if(tv == NULL || timeval_usecdiff(tv, curr->frame_timeout) < 0)
                tv = curr->frame_timeout;
        }
    }

    return tv;
}


void handle_incoming_acks(Sender * sender,
        LLnode ** outgoing_frames_head_ptr)
{
    int incoming_msgs_length = ll_get_length(sender->input_framelist_head);
    while(incoming_msgs_length > 0)
    {
        //Receive the ACK and update the list length
        LLnode * ll_inmsg_node = ll_pop_node(&sender->input_framelist_head);
        incoming_msgs_length = ll_get_length(sender->input_framelist_head);

        //Convert the ACK node to an actual frame
        char * raw_char_buf = (char *) ll_inmsg_node->value;
        Frame * inframe = convert_char_to_frame(raw_char_buf);

        //Don't need the node anymore
        ll_destroy_node(ll_inmsg_node);

        //Checks if the frame is corrupted
        if(crc8(raw_char_buf, MAX_FRAME_SIZE))
        {
            //Checks if this ACK is for this sender
            if(atoi(inframe->sender_addr) == sender->send_id)
            {
                int id = atoi(inframe->receiver_addr);
                int seqnum = (int) inframe->seqnum;

                if(is_valid_sender(sender, seqnum, id))
                {
                    fprintf(stderr, "ACK %d RECEIVED\n", seqnum);
                    send_Q** buf = sender->send_q_head[id];
                    int lar = sender->LAR[id];
                    while(lar != seqnum)
                    {
                        lar = (lar == MAX_SEQ_NUM) ? 0 : lar + 1;
                        free(buf[lar % WS]->frame);
                        free(buf[lar % WS]->frame_timeout);
                        buf[lar % WS]->frame = NULL;
                        buf[lar % WS]->frame_timeout = NULL;
                    }
                    sender->LAR[id] = lar;
                }
            }
        }

        free(inframe);
    }
}


void handle_input_cmds(Sender * sender,
        LLnode ** outgoing_frames_head_ptr)
{
    int input_cmd_length = ll_get_length(sender->input_cmdlist_head);

    //Split the message if it's too long
    ll_split_head(&sender->input_cmdlist_head, FRAME_PAYLOAD_SIZE - 1);

    //Recheck the command queue length to see if stdin_thread dumped a command on us
    input_cmd_length = ll_get_length(sender->input_cmdlist_head);

    while (input_cmd_length > 0) 
    {
        //Pop a node off and update the input_cmd_length
        LLnode * ll_input_cmd_node = ll_pop_node(&sender->input_cmdlist_head);
        input_cmd_length = ll_get_length(sender->input_cmdlist_head);

        //Cast to Cmd type and free up the memory for the node
        Cmd * outgoing_cmd = (Cmd *) ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        //Convert uint16_t to string
        char* src = malloc(MAC_ADDR_SIZE);
        char* dst = malloc(MAC_ADDR_SIZE);
        sprintf(src, "%u", outgoing_cmd->src_id);
        sprintf(dst, "%u", outgoing_cmd->dst_id);

        int id = atoi(dst);

        if(send_q_size(sender, id) < WS)
        {
            Frame * outgoing_frame = (Frame *) malloc (sizeof(Frame));

            //Populate the outgoing frame's fields
            strncpy(outgoing_frame->receiver_addr, dst, MAC_ADDR_SIZE);
            strncpy(outgoing_frame->sender_addr, src, MAC_ADDR_SIZE);
            strncpy(outgoing_frame->data, outgoing_cmd->message, FRAME_PAYLOAD_SIZE);

            //Assign seqnum to the outgoing frame
            outgoing_frame->seqnum = sender->seqnum[id];

            char* raw_char_buf = convert_frame_to_char(outgoing_frame); 

            //Assign crc to the outgoing frame
            outgoing_frame->crc = crc8(raw_char_buf, MAX_FRAME_SIZE);

            free(raw_char_buf);

            //At this point, we don't need the outgoing_cmd
            free(outgoing_cmd->message);
            free(outgoing_cmd);

            //Update LFS
            sender->LFS[id] = sender->seqnum[id];

            //Convert the message to the outgoing_charbuf and send it
            char * outgoing_charbuf = convert_frame_to_char(outgoing_frame);
            //fprintf(stderr, "MESSAGE %s SENT\n", outgoing_frame->data);
            ll_append_node(outgoing_frames_head_ptr, outgoing_charbuf);

            sender->send_q_head[id][sender->seqnum[id] % WS]->frame = malloc(sizeof(Frame));

            //Store the sent frame in the sent queue
            memcpy(sender->send_q_head[id][sender->seqnum[id] % WS]->frame, outgoing_frame, sizeof(Frame)); 
            sender->send_q_head[id][sender->seqnum[id] % WS]->frame_timeout = NULL;

            free(outgoing_frame);
            sender->seqnum[id] = (sender->seqnum[id] == 255) ? 0 : sender->seqnum[id] + 1;
        }
        else
        {
            ll_append_node_at_front(&sender->input_cmdlist_head, outgoing_cmd);
            input_cmd_length = ll_get_length(sender->input_cmdlist_head);
            break;
        }
    }
}

void handle_timedout_frames(Sender * sender,
        LLnode ** outgoing_frames_head_ptr)
{
    //Iterate through the send queue
    int i;
    for(i=0; i < glb_receivers_array_length; i++)
    {
        send_Q ** curr_node = sender->send_q_head[i];
        int lar = sender->LAR[i];
        int lfs = sender->LFS[i];
        while(lar != lfs)
        {
            lar = (lar + 1) % (MAX_SEQ_NUM + 1);
            send_Q * curr = curr_node[lar%WS];
            Frame * sent_frame = curr->frame;
            struct timeval * tv = curr->frame_timeout;
            struct timeval now;
            gettimeofday(&now, NULL);

            //A frame with set timeout is found
            if(tv != NULL)
            {
                //Update timeout field for old, timed out frames
                if(timeval_usecdiff(tv, &now) > 0)
                {
                    fprintf(stderr, "TIMEDOUT DATA %s being resent\n", sent_frame->data);
                    char * outframe_char_buf = convert_frame_to_char(sent_frame);
                    ll_append_node(outgoing_frames_head_ptr, outframe_char_buf);
                    calculate_timeout(curr->frame_timeout);
                }
            }

            //Update timeout field for fresh outgoing frames
            if(tv == NULL && sent_frame != NULL)
            {
                fprintf(stderr, "FRESH DATA %s being sent\n", sent_frame->data);
                curr->frame_timeout = (struct timeval *) malloc(sizeof(struct timeval));
                calculate_timeout(curr->frame_timeout);
            }
        }
    }
}

int is_valid_sender(Sender *sender, int ack, int id)
{
    int LAR = sender->LAR[id];
    int LFS = sender->LFS[id];
    if(LAR <= LFS && ack > LAR && ack <= LFS)
    {
        return 1;
    }
    else if(LAR > LFS && (ack > LAR || ack <= LFS))
    {
        return 1;
    }

    return 0;
}

//Splits long message into smaller messages that fit into frames
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

    //Actual message splitting part
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

    //Cut off the first message with null character
    head_cmd->message[payload_size] = '\0';
}

//Gets the size of sender queue
int send_q_size(Sender * sender, int id)
{
    //Nothing in queue
    if(sender->LFS[id] == sender->LAR[id])
        return 0;

    //Normal case
    else if(sender->LFS[id] > sender->LAR[id])
    {
        return sender->LFS[id] - sender->LAR[id]; 
    }

    //Wrap around case
    else
    {
        return sender->LFS[id] + MAX_SEQ_NUM - sender->LAR[id] + 1; 
    }
}

//Checks if received ACK is cumulative
int is_next_ack(Sender* sender, int ack, int id)
{
    int next_ack = (sender->LAR[id] == 255) ? 0 : sender->LAR[id] + 1;
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


