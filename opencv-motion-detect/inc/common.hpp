#ifndef __COMMON_H__
#define __COMMON_H__
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
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
        int cnt = 0;
        //data
        int wholeSize = 4 + pkt.size;
        //side data
        wholeSize +=4;
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                wholeSize += pkt.side_data[i].size + sizeof(AVPacketSideData);
            }
        }

        // 4 + 8: wholeSize + DEADBEAF
        wholeSize += 8 * 5 + 4 + 4 + 8;
        *bytes = (char*)malloc(wholeSize);

        // data
        memcpy((*bytes)+cnt, &(pkt.size), 4);
        cnt +=4;
        memcpy((*bytes )+cnt, pkt.data, pkt.size);
        cnt += pkt.size;
        //side data
        memcpy((*bytes )+cnt, &(pkt.side_data_elems), 4);
        cnt +=4;
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                memcpy((*bytes )+cnt, &(pkt.side_data[i].size), 4);
                cnt+=4;
                memcpy((*bytes )+cnt, pkt.side_data[i].data, pkt.side_data[i].size);
                cnt+=pkt.side_data[i].size;
                memcpy((*bytes )+cnt, &(pkt.side_data[i].type), 4);
                cnt+=4;
            }
        }

        // other properties
        memcpy((*bytes )+cnt, &(pkt.pts), 8);
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.dts), 8);
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.pos), 8);
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.duration), 8);
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.convergence_duration), 8);
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.flags), 4);
        cnt+=4;
        memcpy((*bytes )+cnt,&wholeSize, 4);
        cnt+=4;
        memcpy((*bytes )+cnt, (char*)"DEADBEEF", 8);
        cnt+=8;
        av_log_set_level(AV_LOG_DEBUG);
        assert(cnt == wholeSize);
        av_log(NULL, AV_LOG_DEBUG, "\n\n\npkt origin size %d, serialized size: %d, elems:%d\n\n\n", pkt.size, wholeSize, pkt.side_data_elems);
        return wholeSize;
    }

    int decode(char * bytes, int len, AVPacket *pkt) {
        // allocate packet mem on heap
        //AVPacket *pkt = (AVPacket*)malloc(sizeof(AVPacket));
        int ret = 0;
        int got = 0;
        if(strncmp("DEADBEEF", bytes + len - 8, 8) != 0) {
            av_log(NULL, AV_LOG_ERROR, "invalid packet");
            return -1;
        }
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

        int wholeSize = 0;
        memcpy(&wholeSize, bytes + got, 4);
        got +=4;
        got+=8;
        av_log(NULL, AV_LOG_WARNING, "wholeSize: %d, %d\n", wholeSize, got);

        return ret;
    }
}

void mqPacketFree(void *data, void*hint) {
    free(data);
}

#endif

