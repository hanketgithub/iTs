#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include "bits.h"

using namespace std;


#if 0
typedef struct
{
    uint32_t m_num_held_bits;
    uint8_t  m_held_bits;
    uint32_t m_numBitsRead;

    uint8_t *m_fifo;
    uint32_t m_fifo_idx;
    uint32_t m_fifo_size;
} InputBitstream_t;


/**
 * TComInputBitstream::read() in HM
 *
 * read_bits(n) in H.265 spec
 */
static uint32_t read_bits(InputBitstream_t &bitstream, uint32_t uiNumberOfBits)
{
    //assert(uiNumberOfBits <= 32);
    
    bitstream.m_numBitsRead += uiNumberOfBits;

    /* NB, bits are extracted from the MSB of each byte. */
    uint32_t retval = 0;
    
    if (uiNumberOfBits <= bitstream.m_num_held_bits)
    {
        /* n=1, len(H)=7:   -VHH HHHH, shift_down=6, mask=0xfe
         * n=3, len(H)=7:   -VVV HHHH, shift_down=4, mask=0xf8
         */
        retval = bitstream.m_held_bits >> (bitstream.m_num_held_bits - uiNumberOfBits);
        retval &= ~(0xff << uiNumberOfBits);
        bitstream.m_num_held_bits -= uiNumberOfBits;
        
        return retval;
    }
    
    /* all num_held_bits will go into retval
     *   => need to mask leftover bits from previous extractions
     *   => align retval with top of extracted word */
    /* n=5, len(H)=3: ---- -VVV, mask=0x07, shift_up=5-3=2,
     * n=9, len(H)=3: ---- -VVV, mask=0x07, shift_up=9-3=6 */
    uiNumberOfBits -= bitstream.m_num_held_bits;
    retval = bitstream.m_held_bits & ~(0xff << bitstream.m_num_held_bits);
    retval <<= uiNumberOfBits;
    
    /* number of whole bytes that need to be loaded to form retval */
    /* n=32, len(H)=0, load 4bytes, shift_down=0
     * n=32, len(H)=1, load 4bytes, shift_down=1
     * n=31, len(H)=1, load 4bytes, shift_down=1+1
     * n=8,  len(H)=0, load 1byte,  shift_down=0
     * n=8,  len(H)=3, load 1byte,  shift_down=3
     * n=5,  len(H)=1, load 1byte,  shift_down=1+3
     */
    uint32_t aligned_word = 0;
    uint32_t num_bytes_to_load = (uiNumberOfBits - 1) >> 3;
    
    //assert(m_fifo_idx + num_bytes_to_load < m_fifo->size());
    
    switch (num_bytes_to_load)
    {
        case 3: aligned_word  = (bitstream.m_fifo)[bitstream.m_fifo_idx++] << 24;
        case 2: aligned_word |= (bitstream.m_fifo)[bitstream.m_fifo_idx++] << 16;
        case 1: aligned_word |= (bitstream.m_fifo)[bitstream.m_fifo_idx++] << 8;
        case 0: aligned_word |= (bitstream.m_fifo)[bitstream.m_fifo_idx++];
    }
    
    /* resolve remainder bits */
    uint32_t next_num_held_bits = (32 - uiNumberOfBits) % 8;
    
    /* copy required part of aligned_word into retval */
    retval |= aligned_word >> next_num_held_bits;
    
    /* store held bits */
    bitstream.m_num_held_bits = next_num_held_bits;
    bitstream.m_held_bits = (uint8_t) (aligned_word & 0xFF);

    //printf("m_num_held_bits=%d\n", next_num_held_bits);

    return retval;
}
#endif


int main(int argc, char *argv[])
{
    int fd;
    uint16_t VPID;
    ssize_t rd_sz;
        
    if (argc < 3)
    {
        printf("useage: %s [input_file] [VPID]\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        perror(argv[1]);
        exit(-1);
    }

    VPID = atoi(argv[2]);

    struct stat st;
    uint32_t file_size = 0;

    if (stat(argv[1], &st) == 0)
    {
        file_size = st.st_size;
    }
    else
    {
        perror(argv[1]);
        exit(-1);
    }

    uint8_t *data = (uint8_t *) calloc(1, file_size);

    rd_sz = read(fd, data, file_size);

    uint8_t *ptr = data;

    uint8_t last_continuity_counter = 0xF;
    for (; ptr - data < file_size; ptr += 188) {
        InputBitstream_t ibs = {0};

        ibs.m_fifo = ptr;

        uint8_t sync_byte = READ_CODE(ibs, 8, "sync_byte");
        uint8_t transport_error_indicator = READ_CODE(ibs, 1, "transport_error_indicator");
        uint8_t payload_unit_start_indicator = READ_CODE(ibs, 1, "");
        uint8_t transport_priority = READ_CODE(ibs, 1, "");
        uint16_t PID = READ_CODE(ibs, 13, "");
        uint8_t transport_scrambling_control = READ_CODE(ibs, 2, "");
        uint8_t adaptation_field_control = READ_CODE(ibs, 2, "");
        uint8_t continuity_counter = READ_CODE(ibs, 4, "");

        if (sync_byte != 0x47)
        {
            printf("Invalid TS stream at 0x%lx, please chk!\n", ptr - data);
            exit(-1);
        }


        printf("transport_error_indicator=%d payload_unit_start_indicator=%d transport_priority=%d PID=0x%x\n", transport_error_indicator, payload_unit_start_indicator, transport_priority, PID);

        if (PID == VPID)
        {
            printf("transport_scrambling_control=%d adaptation_field_control=%d continuity_counter=%d\n", transport_scrambling_control, adaptation_field_control, continuity_counter);

            if (adaptation_field_control == 3)
            {
                printf("Offset=0x%lx\n", ptr-data);
                uint8_t adaptation_extension_length = READ_CODE(ibs, 8, "");

                // copy what's leftover.
                uint8_t *copy = ptr + 6;
                uint32_t cpSize = 188 - 6 - adaptation_extension_length;
                // copy from copy ptr & cpSize, which is the leftover ES;
                
            }

            if (last_continuity_counter != 0xF && (last_continuity_counter+1) % 16 != continuity_counter)
            {
                printf("Discontinous counter! Offset=0x%lx\n", ptr-data);
            }
            else
            {
                last_continuity_counter = continuity_counter;
            }
        }
    }

    return 0;
}