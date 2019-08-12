#ifndef __COMMON_H__
#define __COMMON_H__
extern "C" {
#include <libavformat/avformat.h>
}
#include <libavutil/timestamp.h>
#undef av_err2str
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)

void logThrow(void * avcl, int lvl, const char *fmt, ...)
{
    (void) avcl;
    (void) lvl;
    va_list args;
    va_start( args, fmt );
    av_log(NULL, AV_LOG_FATAL, fmt, args);
    va_end( args );
    throw fmt;
}

namespace PacketSerializer {
    int encode(AVPacket &pkt, char **bytes) {
        int wholeSize = 4 + pkt.size;
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                wholeSize += pkt.side_data[i].size + sizeof(AVPacketSideData);
            }
        }else{
            wholeSize +=4;
        }
        wholeSize += 8 * 5 + 4;
        *bytes = (char*)malloc(wholeSize);

        // data
        memcpy(*bytes, &(pkt.size), 4);
        memcpy(*bytes, pkt.data, pkt.size);
        //side data
        memcpy(*bytes, &(pkt.side_data_elems), 4);
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                memcpy(*bytes, &(pkt.side_data[i].size), 4);
                memcpy(*bytes, pkt.side_data[i].data, pkt.side_data[i].size);
                memcpy(*bytes, &(pkt.side_data[i].type), 4);
            }
        }else{
            wholeSize +=4;
        }

        // other properties
        memcpy(*bytes, &(pkt.pts), 8);
        memcpy(*bytes, &(pkt.dts), 8);
        memcpy(*bytes, &(pkt.pos), 8);
        memcpy(*bytes, &(pkt.duration), 8);
        memcpy(*bytes, &(pkt.convergence_duration), 8);
        memcpy(*bytes, &(pkt.flags), 4);
        av_log_set_level(AV_LOG_DEBUG);
        av_log(NULL, AV_LOG_DEBUG, "\n\n\npkt origin size %d, serialized size: %d, elems:%d\n\n\n", pkt.size, wholeSize, pkt.side_data_elems);
        return wholeSize;
    }

    AVPacket *decode(char * bytes) {
        // allocate packet mem on heap
        AVPacket *pkt = (AVPacket*)malloc(sizeof(AVPacket));
        int got = 0;
        memcpy(&(pkt->size), bytes, 4);
        got += 4;
        av_new_packet(pkt, pkt->size);
        memcpy(pkt->data, bytes + got, pkt->size);
        got += pkt->size;
        memcpy(&pkt->side_data_elems, bytes + got, 4);
        got += 4;
        for(int i = 0; i < pkt->side_data_elems; i++) {
            memcpy(&(pkt->side_data[i].size), bytes+got, 4);
            got += 4;
            memcpy(pkt->side_data[i].data,bytes + got ,pkt->side_data[i].size);
            got += pkt->side_data[i].size;
            memcpy(&(pkt->side_data[i].type), bytes + got, 4);
            got += 4;
        }

        // props
        memcpy(&(pkt->pts), bytes + got, 8);
        got += 8;
        memcpy(&(pkt->dts), bytes + got, 8);
        got += 8;
        memcpy(&(pkt->pos), bytes + got, 8);
        got += 8;
        memcpy(&(pkt->duration), bytes + got, 8);
        got += 8;
        memcpy(&(pkt->convergence_duration), bytes + got, 8);
        got += 8;
        memcpy(&(pkt->flags), bytes + got, 4);
        got += 4;

        return pkt;
    }
}

void mqPacketFree(void *data, void*hint) {
    free(data);
}

#endif

