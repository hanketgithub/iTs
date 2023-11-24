#ifndef ___I_TS_BITS_H___
#define ___I_TS_BITS_H___


typedef struct
{
    uint32_t m_num_held_bits;
    uint8_t  m_held_bits;
    uint32_t m_numBitsRead;

    uint8_t *m_fifo;
    uint32_t m_fifo_idx;
    uint32_t m_fifo_size;
} InputBitstream_t;


uint32_t READ_CODE
(
    InputBitstream_t &bitstream,
    uint32_t length, 
    const char *name
);


#endif

