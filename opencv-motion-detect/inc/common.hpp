#ifndef __COMMON_H__
#define __COMMON_H__
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}
#include <libavutil/timestamp.h>

#undef av_err2str
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)

#define PS_MARK_E "DEADBEEF"
#define PS_MARK_S "BEEFDEAD"

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

namespace AVPacketSerializer {
    int encode(AVPacket &pkt, char **bytes) {
        int cnt = 0;
        //data
        char mark_s[] = PS_MARK_S;
        char mark_e[] = PS_MARK_E;
        int wholeSize = sizeof(mark_s) - 1 + sizeof(pkt.size) + pkt.size;
        //side data
        wholeSize +=sizeof(pkt.side_data_elems);
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                wholeSize += pkt.side_data[i].size + sizeof(AVPacketSideData);
            }
        }

        // 4 + 8: wholeSize + DEADBEAF
        wholeSize += sizeof(pkt.pts) * 5 + sizeof(pkt.flags) + sizeof(pkt.stream_index) + sizeof(wholeSize) + sizeof(mark_e) -1;
        *bytes = (char*)malloc(wholeSize);

        memcpy((*bytes)+cnt, mark_s, sizeof(mark_s) -1);
        cnt += sizeof(mark_s) -1;
        // data
        memcpy((*bytes)+cnt, &(pkt.size), sizeof(pkt.size));
        cnt +=sizeof(pkt.size);
        memcpy((*bytes )+cnt, pkt.data, pkt.size);
        cnt += pkt.size;
        //side data
        memcpy((*bytes )+cnt, &(pkt.side_data_elems), sizeof(pkt.side_data_elems));
        cnt += sizeof(pkt.side_data_elems);
        if(pkt.side_data_elems != 0) {
            for(int i = 0; i < pkt.side_data_elems; i++) {
                memcpy((*bytes )+cnt, &(pkt.side_data[i].size), sizeof(pkt.side_data[i].size));
                cnt+=sizeof(pkt.side_data[i].size);
                memcpy((*bytes )+cnt, pkt.side_data[i].data, pkt.side_data[i].size);
                cnt+=pkt.side_data[i].size;
                memcpy((*bytes )+cnt, &(pkt.side_data[i].type), sizeof(pkt.side_data[i].type));
                cnt+=sizeof(pkt.side_data[i].type);
            }
        }

        // other properties
        memcpy((*bytes )+cnt, &(pkt.pts), sizeof(pkt.pts));
        cnt+=sizeof(pkt.pts);
        memcpy((*bytes )+cnt, &(pkt.dts), sizeof(pkt.dts));
        cnt+=8;
        memcpy((*bytes )+cnt, &(pkt.pos), sizeof(pkt.pos));
        cnt+=sizeof(pkt.pos);
        memcpy((*bytes )+cnt, &(pkt.duration), sizeof(pkt.duration));
        cnt+=sizeof(pkt.duration);
        memcpy((*bytes )+cnt, &(pkt.convergence_duration), sizeof(pkt.convergence_duration));
        cnt+=sizeof(pkt.convergence_duration);
        memcpy((*bytes )+cnt, &(pkt.flags), sizeof(pkt.flags));
        cnt+=sizeof(pkt.flags);
        memcpy((*bytes )+cnt, &(pkt.stream_index), sizeof(pkt.stream_index));
        cnt+=sizeof(pkt.stream_index);
        memcpy((*bytes )+cnt,&wholeSize, sizeof(wholeSize));
        cnt+=sizeof(wholeSize);
        memcpy((*bytes )+cnt, mark_e, sizeof(mark_e) -1);
        cnt+=sizeof(mark_e) -1;
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
        char mark_s[] = PS_MARK_S;
        char mark_e[] = PS_MARK_E;
        if(memcmp(mark_e, bytes + len - sizeof(mark_e) + 1, sizeof(mark_e) - 1) != 0 || memcmp(mark_s, bytes, sizeof(mark_s) - 1)) {
            av_log(NULL, AV_LOG_ERROR, "invalid packet");
            return -1;
        }
        //skip mark_s
        got += sizeof(mark_s) - 1;
        memcpy(&(pkt->size), bytes + got, sizeof(pkt->size));
        got += sizeof(pkt->size);
        av_new_packet(pkt, pkt->size);
        memcpy(pkt->data, bytes + got, pkt->size);
        got += pkt->size;
        memcpy(&pkt->side_data_elems, bytes + got, sizeof(pkt->side_data_elems));
        got += sizeof(pkt->side_data_elems);
        for(int i = 0; i < pkt->side_data_elems; i++) {
            memcpy(&(pkt->side_data[i].size), bytes+got, sizeof(pkt->side_data[i].size));
            got += sizeof(pkt->side_data[i].size);
            memcpy(pkt->side_data[i].data,bytes + got ,pkt->side_data[i].size);
            got += pkt->side_data[i].size;
            memcpy(&(pkt->side_data[i].type), bytes + got, sizeof(pkt->side_data[i].type));
            got += sizeof(pkt->side_data[i].type);
        }

        // props
        memcpy(&(pkt->pts), bytes + got, sizeof(pkt->pts));
        got += sizeof(pkt->pts);
        memcpy(&(pkt->dts), bytes + got, sizeof(pkt->dts));
        got += sizeof(pkt->dts);
        memcpy(&(pkt->pos), bytes + got, sizeof(pkt->pos));
        got += sizeof(pkt->pos);
        memcpy(&(pkt->duration), bytes + got, sizeof(pkt->duration));
        got += sizeof(pkt->duration);
        memcpy(&(pkt->convergence_duration), bytes + got, sizeof(pkt->convergence_duration));
        got += sizeof(pkt->convergence_duration);
        memcpy(&(pkt->flags), bytes + got, sizeof(pkt->flags));
        got += sizeof(pkt->flags);
        memcpy(&(pkt->stream_index), bytes + got, sizeof(pkt->stream_index));
        got += sizeof(pkt->stream_index);

        int wholeSize = 0;
        memcpy(&wholeSize, bytes + got, sizeof(wholeSize));
        got += sizeof(wholeSize);
        got += 8;
        av_log(NULL, AV_LOG_WARNING, "wholeSize: %d, %d\n", wholeSize, got);

        return ret;
    }
}

void mqPacketFree(void *data, void*hint) {
    free(data);
}

namespace AVFormatCtxSerializer {
    struct AVFormatCtx {
        AVFormatCtx() {};
        AVFormatCtx(char *bytes, int len) {
            _decode(bytes, len);
        }

        AVFormatCtx(AVFormatContext *pCtx):pCtx(pCtx) {
        }

        ~AVFormatCtx() {
        }

        int toBytes(char **bytes) {
            return _encode(pCtx, bytes);
        }

        int fromBytes(char *bytes, int len) {
            return _decode(bytes, len);
        }

        private:
            int nb_streams;
            AVStream *streams = NULL;
            AVFormatContext * pCtx = NULL;
            int _encode(AVFormatContext *ctx, char **bytes) {
                int ret = 0;
                int wholeSize = 0;
                int got = 0;
                char mark_s[] = PS_MARK_S;
                char mark_e[] = PS_MARK_E;

                wholeSize += sizeof(mark_s) - 1;
                // num streams
                wholeSize += sizeof(pCtx->nb_streams);
                for(int i = 0; i < pCtx->nb_streams; i++) {
                    wholeSize += sizeof(AVStream);
                }

                wholeSize += sizeof(wholeSize); 
            }

            int _decode(char *bytes, int len) {
            }
    };
}

#endif

