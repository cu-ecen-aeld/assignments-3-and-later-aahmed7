/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

// #include <stdio.h>
// #define DEBUG_LOG(msg,...) printf("circular-buffer DEBUG: " msg "\n" , ##__VA_ARGS__)

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    int i = buffer->out_offs;
    do {

        // DEBUG_LOG("i: %d", i);
        // DEBUG_LOG("offset: %lu", char_offset);

        if (char_offset < buffer->entry[i].size){
            *entry_offset_byte_rtn = char_offset;
            return &buffer->entry[i];
        }else{
            char_offset -= buffer->entry[i].size;
        }
        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (i != buffer->in_offs);
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // if buffer is full, we move the out_offs.
    if (buffer->full) buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If out_offs and in_offs become equal, this indicates that the buffer is now full.
    // This is the signal to move out_offs the next time an entry is added.
    if (buffer->in_offs == buffer->out_offs) buffer->full = true;

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

// static void write_circular_buffer_packet(struct aesd_circular_buffer *buffer,
//                                          const char *writestr)
// {
//     struct aesd_buffer_entry entry;
//     entry.buffptr = writestr;
//     entry.size=strlen(writestr);
//     aesd_circular_buffer_add_entry(buffer,&entry);
// }


// int main(void){
//     struct aesd_circular_buffer buffer;
//     size_t offset_rtn=0;
//     aesd_circular_buffer_init(&buffer);
//     write_circular_buffer_packet(&buffer,"write1\n"); 
//     write_circular_buffer_packet(&buffer,"write2\n"); 
//     write_circular_buffer_packet(&buffer,"write3\n"); 
//     write_circular_buffer_packet(&buffer,"write4\n"); 
//     write_circular_buffer_packet(&buffer,"write5\n"); 
//     write_circular_buffer_packet(&buffer,"write6\n"); 
//     write_circular_buffer_packet(&buffer,"write7\n"); 
//     write_circular_buffer_packet(&buffer,"write8\n"); 
//     write_circular_buffer_packet(&buffer,"write9\n"); 
//     write_circular_buffer_packet(&buffer,"write10\n"); 
//     struct aesd_buffer_entry *rtnentry = aesd_circular_buffer_find_entry_offset_for_fpos(&buffer,
//                                                 0,
//                                                 &offset_rtn);
// }