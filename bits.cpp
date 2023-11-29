//
//  bits.cpp
//  iTs
//
//  Created by Hank Lee on 2023/10/05.
//  Copyright (c) 2023 hank. All rights reserved.
//

#include <stdint.h>
#include <stdio.h>

#include "bits.h"

using namespace std;


int dbg = 1;

#define min(a, b) (((a) < (b)) ? (a) : (b)) 

#define TRACE(fmt, name, val)   \
    if (dbg > 0)                \
        fprintf(stdout, fmt, name, val);

#define TRACE_4(fmt, name, len, val)   \
    if (dbg > 0)                \
        fprintf(stdout, fmt, name, len, val);


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


uint32_t READ_CODE
(
    InputBitstream_t &bitstream,
    uint32_t length, 
    const char *name
)
{
    uint32_t ret;
    
    ret = read_bits(bitstream, length);

    TRACE_4("%-50s u(%d)  : %u\n", name, length, ret);

    return ret;
}


bool READ_FLAG
(
    InputBitstream_t &bitstream,
    const char *name
)
{
    bool ret;

    ret = (bool) (read_bits(bitstream, 1) & 0x01);
    
    TRACE("%-50s u(1)  : %u\n", name, ret);

    return ret;
}

