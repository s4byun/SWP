#include "util.h"

//Linked list functions
int ll_get_length(LLnode * head)
{
    LLnode * tmp;
    int count = 1;
    if (head == NULL)
        return 0;
    else
    {
        tmp = head->next;
        while (tmp != head)
        {
            count++;
            tmp = tmp->next;
        }
        return count;
    }
}

void ll_append_node(LLnode ** head_ptr, 
                    void * value)
{
    LLnode * prev_last_node;
    LLnode * new_node;
    LLnode * head;

    if (head_ptr == NULL)
    {
        return;
    }
    
    //Init the value pntr
    head = (*head_ptr);
    new_node = (LLnode *) malloc(sizeof(LLnode));
    new_node->value = value;

    //The list is empty, no node is currently present
    if (head == NULL)
    {
        (*head_ptr) = new_node;
        new_node->prev = new_node;
        new_node->next = new_node;
    }
    else
    {
        //Node exists by itself
        prev_last_node = head->prev;
        head->prev = new_node;
        prev_last_node->next = new_node;
        new_node->next = head;
        new_node->prev = prev_last_node;
    }
}


LLnode * ll_pop_node(LLnode ** head_ptr)
{
    LLnode * last_node;
    LLnode * new_head;
    LLnode * prev_head;

    prev_head = (*head_ptr);
    if (prev_head == NULL)
    {
        return NULL;
    }
    last_node = prev_head->prev;
    new_head = prev_head->next;

    //We are about to set the head ptr to nothing because there is only one thing in list
    if (last_node == prev_head)
    {
        (*head_ptr) = NULL;
        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    }
    else
    {
        (*head_ptr) = new_head;
        last_node->next = new_head;
        new_head->prev = last_node;

        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    }
}

void ll_destroy_node(LLnode * node)
{
    if (node->type == llt_string)
    {
        free((char *) node->value);
    }
    free(node);
    node = NULL;
}

void ll_destroy_sendQ(LLnode * node)
{
    send_Q * curr = (send_Q *) node->value;
    free(curr->frame);
    free(curr->frame_timeout);
    curr->frame = NULL;
    curr->frame_timeout = NULL;
    free(curr);
    curr = NULL;
    free(node);
    node = NULL;
}

//Compute the difference in usec for two timeval objects
long timeval_usecdiff(struct timeval *start_time, 
                      struct timeval *finish_time)
{
  long usec;
  usec=(finish_time->tv_sec - start_time->tv_sec)*1000000;
  usec+=(finish_time->tv_usec- start_time->tv_usec);
  return usec;
}


//Print out messages entered by the user
void print_cmd(Cmd * cmd)
{
    fprintf(stderr, "src=%d, dst=%d, message=%s\n", 
           cmd->src_id,
           cmd->dst_id,
           cmd->message);
}


char * convert_frame_to_char(Frame * frame)
{
    //TODO: You should implement this as necessary
    char * char_buf = (char *) malloc(MAX_FRAME_SIZE);

    memset(char_buf, 0, MAX_FRAME_SIZE);
    memcpy(char_buf, frame->receiver_addr, MAC_ADDR_SIZE);
    memcpy(char_buf + MAC_ADDR_SIZE, frame->sender_addr, MAC_ADDR_SIZE);
    memcpy(char_buf + MAC_ADDR_SIZE*2, frame->data, FRAME_PAYLOAD_SIZE);
    memcpy(char_buf + MAC_ADDR_SIZE*2 + FRAME_PAYLOAD_SIZE, &frame->seqnum, SEQ_NUM_SIZE);
    memcpy(char_buf + MAC_ADDR_SIZE*2 + FRAME_PAYLOAD_SIZE + SEQ_NUM_SIZE, &frame->crc,
            CRC_SIZE);

    return char_buf;
}


Frame * convert_char_to_frame(char * char_buf)
{
    //TODO: You should implement this as necessary
    Frame * frame = (Frame *) malloc(sizeof(Frame));
    memset(frame, 0, MAX_FRAME_SIZE);

    memcpy(frame->receiver_addr, char_buf, MAC_ADDR_SIZE);
    memcpy(frame->sender_addr, char_buf + MAC_ADDR_SIZE, MAC_ADDR_SIZE);
    memcpy(frame->data, char_buf + MAC_ADDR_SIZE*2, FRAME_PAYLOAD_SIZE);
    memcpy(&frame->seqnum, char_buf + MAC_ADDR_SIZE*2 + FRAME_PAYLOAD_SIZE, SEQ_NUM_SIZE);
    memcpy(&frame->crc, char_buf + MAC_ADDR_SIZE*2 + FRAME_PAYLOAD_SIZE + SEQ_NUM_SIZE, CRC_SIZE);
    return frame;
}

#define WIDTH 8
char crc8(char* array, int length)
{
    char poly = 0x07;
    char crc = array[0];

    int i, j;
    
    for(i=1; i < length; ++i)
    {
        char next_byte = (i == length-1) ? 0 : array[i];
        for(j=WIDTH-1; j >= 0; --j)
        {
            // If most significant bit = 0
            if(!(crc & 0x80))
            {
                crc = crc << 1;
                crc = crc | ((next_byte >> j) & 0x01);
            }
            else
            {
                crc = crc << 1;
                crc = crc | ((next_byte >> j) & 0x01);
                crc = crc ^ poly;
            }
        }
    }
    return crc;
}

void calculate_timeout(struct timeval * timeout)
{
    gettimeofday(timeout, NULL);
    timeout->tv_usec += 100000;
    if(timeout->tv_usec > 1000000)
    {
        timeout->tv_sec += 1;
        timeout->tv_usec -= 1000000;
    }
}
