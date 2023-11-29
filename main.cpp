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

#define TS_PKT_SIZE                         188
#define TS_PKT_HDR_MANDATORY_SIZE           4   // Sync_byte to Counitnuity_counter
#define TS_PKT_HDR_ADAPTION_FIELD_LEN_SIZE  1

#define PES_PKT_HDR_LEN             19   // should not be hard-coded, just for 20230221_original_asi_source_fix_frame_num.ts

using namespace std;


uint8_t *findPattern(uint8_t *src, uint8_t *pattern, uint32_t len)
{
    for (int i = 0; i < len; i++)
    {
        if (memcmp(&src[i], pattern, 4) == 0)
        {
            return &src[i];
        }
    }
    return NULL;
}


bool chkDistance(uint8_t distance, uint8_t adaptation_field_control, uint8_t adaptation_extension_length)
{
    int base_ts_hdr_len = 4;

    if (adaptation_field_control == 3)
    {
        base_ts_hdr_len += 1 + adaptation_extension_length;
    }

    return distance == base_ts_hdr_len;
}


int main(int argc, char *argv[])
{
    int fd;
    uint16_t VPID;
    ssize_t rd_sz;
    vector< pair<uint8_t *, uint32_t> > copyQ;
        
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

    uint8_t *ts_ptr = data;

    uint8_t last_continuity_counter = 0xF;
    for (; ts_ptr - data < file_size; ts_ptr += 188)
    {
        InputBitstream_t ibs = {0};

        ibs.m_fifo = ts_ptr;

        uint8_t sync_byte                       = READ_CODE(ibs, 8, "sync_byte");
        bool transport_error_indicator          = READ_FLAG(ibs, "transport_error_indicator");
        bool payload_unit_start_indicator       = READ_FLAG(ibs, "payload_unit_start_indicator");
        bool transport_priority                 = READ_FLAG(ibs, "transport_priority");
        uint16_t PID                            = READ_CODE(ibs, 13, "PID");
        uint8_t transport_scrambling_control    = READ_CODE(ibs, 2, "transport_scrambling_control");
        uint8_t adaptation_field_control        = READ_CODE(ibs, 2, "adaptation_field_control");
        uint8_t continuity_counter              = READ_CODE(ibs, 4, "continuity_counter");

        if (sync_byte != 0x47)
        {
            printf("Invalid TS stream at 0x%lx, please chk!\n", ts_ptr - data);
            exit(-1);
        }

        if (PID == VPID)
        {
            printf("-----Offset=0x%lx\n", ts_ptr - data);
            //printf("transport_error_indicator=%d payload_unit_start_indicator=%d transport_priority=%d PID=0x%x\n", transport_error_indicator, payload_unit_start_indicator, transport_priority, PID);
            //printf("transport_scrambling_control=%d adaptation_field_control=%d continuity_counter=%d\n", transport_scrambling_control, adaptation_field_control, continuity_counter);

            if (payload_unit_start_indicator)
            {
                uint8_t adaptation_extension_length = 0;

                if (adaptation_field_control == 3)
                {
                    adaptation_extension_length = READ_CODE(ibs, 8, "adaptation_extension_length");
                }

                uint8_t PESstartCode[] = { 0x00, 0x00, 0x01, 0xE0 };

                uint8_t *pes = findPattern(&ibs.m_fifo[TS_PKT_HDR_MANDATORY_SIZE + adaptation_extension_length], PESstartCode, TS_PKT_SIZE - (TS_PKT_HDR_MANDATORY_SIZE + adaptation_extension_length));
                if (pes == NULL)
                {
                    printf("fucked up!\n");
                    exit(-1);
                }
                
                //printf("PES found! Offset=0x%lx distance=%ld\n", pes - data, pes - ts_ptr);

                if (!chkDistance(pes - ts_ptr, adaptation_field_control, adaptation_extension_length))
                {
                    exit(-1);
                }

                // copy payload excluding PES header
                uint8_t *copy = pes + PES_PKT_HDR_LEN;
                uint32_t cpSize = ts_ptr + TS_PKT_SIZE - copy;

                copyQ.push_back( {copy, cpSize} );
            }
            else if (adaptation_field_control == 3)
            {
                uint8_t adaptation_extension_length = READ_CODE(ibs, 8, "adaptation_extension_length");

                // copy what's leftover.
                uint32_t cpSize = TS_PKT_SIZE - (TS_PKT_HDR_MANDATORY_SIZE + TS_PKT_HDR_ADAPTION_FIELD_LEN_SIZE) - adaptation_extension_length;
                uint8_t *copy = ts_ptr + TS_PKT_SIZE - cpSize;

                copyQ.push_back( {copy, cpSize} );   
            }
            else if (adaptation_field_control == 2)
            {
                // do not copy anything
            }
            else
            {
                // copy what's leftover.
                uint32_t cpSize = TS_PKT_SIZE - TS_PKT_HDR_MANDATORY_SIZE;
                uint8_t *copy = ts_ptr + TS_PKT_HDR_MANDATORY_SIZE;

                copyQ.push_back( {copy, cpSize} );
            }

            if (last_continuity_counter != 0xF && (last_continuity_counter+1) % 16 != continuity_counter)
            {
                printf("Discontinous counter! Offset=0x%lx\n", ts_ptr-data);
            }
            else
            {
                last_continuity_counter = continuity_counter;
            }
        }
    }

    // Flush output
    {
        char output[256];
        char *cp = strrchr(argv[1], '.');

        strncpy(output, argv[1], cp - argv[1]);
        strcat(output, "_fix_frame_num");
        strcat(output, cp);

        int ofd = open(output, O_RDWR | O_CREAT, S_IRUSR);

        for (auto [start, len] : copyQ)
        {
            if (start - data > 0x3cb366) // 1st I
            {
                write(ofd, start, len);
                if (start - data > 0x44c994)
                {
                    printf("copy 0x%lx : %d\n", start - data, len);
                    exit(-1);
                }
            }
        }

        close(ofd);
    }

    return 0;
}