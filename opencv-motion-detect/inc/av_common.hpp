/*
module: av_common
description: 
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/


#ifndef __EV_AV_COMMON_H__
#define __EV_AV_COMMON_H__
extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

#include <libavutil/timestamp.h>
#include <spdlog/spdlog.h>
#include <json.hpp>
#include <sstream>
using json = nlohmann::json;



#undef av_err2str
#define av_err2str(errnum) av_make_error_string((char *)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)

#define PS_MARK_E "DEADBEEF"
#define PS_MARK_S "BEEFDEAD"

#define EV_LOG_PACKET_CNT (18*60*5)

void avlogThrow(void *avcl, int lvl, const char *fmt, ...)
{
    (void)avcl;
    (void)lvl;
    va_list args;
    va_start(args, fmt);
    av_log(NULL, AV_LOG_FATAL, fmt, args);
    va_end(args);
    throw fmt;
}

// AVPacketSerializer
namespace AVPacketSerializer
{
int encode(AVPacket &pkt, char **bytes)
{
    int cnt = 0;
    //data
    int wholeSize = strlen(PS_MARK_S) + sizeof(pkt.size) + pkt.size;
    //side data
    wholeSize += sizeof(pkt.side_data_elems);
    if (pkt.side_data_elems != 0)
    {
        spdlog::debug("having side data: {}", pkt.side_data_elems);
        for (int i = 0; i < pkt.side_data_elems; i++)
        {
            wholeSize += pkt.side_data[i].size + sizeof(AVPacketSideData::type) + sizeof(AVPacketSideData::size);
        }
    }

    // 4 + 8: wholeSize + DEADBEAF
    wholeSize += sizeof(pkt.pts) * 4 + sizeof(pkt.flags) + sizeof(pkt.stream_index) + sizeof(wholeSize) + strlen(PS_MARK_E);
    // timestamp
    auto now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
    wholeSize += sizeof(now);
    *bytes = (char *)malloc(wholeSize);

    memcpy((*bytes) + cnt, PS_MARK_S, strlen(PS_MARK_S));
    cnt += strlen(PS_MARK_S);
    // data
    memcpy((*bytes) + cnt, &(pkt.size), sizeof(pkt.size));
    cnt += sizeof(pkt.size);
    memcpy((*bytes) + cnt, pkt.data, pkt.size);
    cnt += pkt.size;
    //side data
    memcpy((*bytes) + cnt, &(pkt.side_data_elems), sizeof(pkt.side_data_elems));
    cnt += sizeof(pkt.side_data_elems);
    if (pkt.side_data_elems != 0)
    {
        for (int i = 0; i < pkt.side_data_elems; i++)
        {
            memcpy((*bytes) + cnt, &(pkt.side_data[i].size), sizeof(pkt.side_data[i].size));
            cnt += sizeof(pkt.side_data[i].size);
            memcpy((*bytes) + cnt, pkt.side_data[i].data, pkt.side_data[i].size);
            cnt += pkt.side_data[i].size;
            memcpy((*bytes) + cnt, &(pkt.side_data[i].type), sizeof(pkt.side_data[i].type));
            cnt += sizeof(pkt.side_data[i].type);
        }
    }

    // other properties
    memcpy((*bytes) + cnt, &(pkt.pts), sizeof(pkt.pts));
    cnt += sizeof(pkt.pts);
    memcpy((*bytes) + cnt, &(pkt.dts), sizeof(pkt.dts));
    cnt += sizeof(pkt.dts);
    memcpy((*bytes) + cnt, &(pkt.pos), sizeof(pkt.pos));
    cnt += sizeof(pkt.pos);
    memcpy((*bytes) + cnt, &(pkt.duration), sizeof(pkt.duration));
    cnt += sizeof(pkt.duration);
    // deprecated
    //memcpy((*bytes )+cnt, &(pkt.convergence_duration), sizeof(pkt.convergence_duration));
    //cnt+=sizeof(pkt.convergence_duration);
    memcpy((*bytes) + cnt, &(pkt.flags), sizeof(pkt.flags));
    cnt += sizeof(pkt.flags);
    memcpy((*bytes) + cnt, &(pkt.stream_index), sizeof(pkt.stream_index));
    cnt += sizeof(pkt.stream_index);
    // ts
    memcpy((*bytes) + cnt, &now, sizeof(now));
    cnt+= sizeof(now);
    // size
    memcpy((*bytes) + cnt, &wholeSize, sizeof(wholeSize));
    cnt += sizeof(wholeSize);
    memcpy((*bytes) + cnt, PS_MARK_E, strlen(PS_MARK_E));
    cnt += strlen(PS_MARK_E);
    if(cnt != wholeSize) {
      spdlog::error("pkt encode : {} != {}", wholeSize, cnt);
    }
    
    assert(cnt == wholeSize);
    av_log(NULL, AV_LOG_DEBUG, "pkt origin size %d, serialized size: %d, elems: %d", pkt.size, wholeSize, pkt.side_data_elems);
    return wholeSize;
}

int decode(char *bytes, int len, AVPacket *pkt, long long *ts = nullptr)
{
    // allocate packet mem on heap
    //AVPacket *pkt = (AVPacket*)malloc(sizeof(AVPacket));
    int ret = 0;
    int got = 0;
    if (memcmp(PS_MARK_E, bytes + len - strlen(PS_MARK_E), strlen(PS_MARK_E)) != 0 || memcmp(PS_MARK_S, bytes, strlen(PS_MARK_S)))
    {
        spdlog::error("invalid packet. len {}", len);
        return -1;
    }
    //skip mark_s
    got += strlen(PS_MARK_S);
    memcpy(&(pkt->size), bytes + got, sizeof(pkt->size));
    got += sizeof(pkt->size);
    av_new_packet(pkt, pkt->size);
    memcpy(pkt->data, bytes + got, pkt->size);
    got += pkt->size;
    memcpy(&pkt->side_data_elems, bytes + got, sizeof(pkt->side_data_elems));
    got += sizeof(pkt->side_data_elems);

    if(pkt->side_data_elems != 0){
      pkt->side_data =(AVPacketSideData*) av_malloc(sizeof(AVPacketSideData) * pkt->side_data_elems );
    }
    for (int i = 0; i < pkt->side_data_elems; i++)
    {
        memcpy(&(pkt->side_data[i].size), bytes + got, sizeof(pkt->side_data[i].size));
        got += sizeof(pkt->side_data[i].size);
        pkt->side_data[i].data = (uint8_t *)av_malloc(pkt->side_data[i].size);
        memcpy(pkt->side_data[i].data, bytes + got, pkt->side_data[i].size);
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
    // deprecated
    //memcpy(&(pkt->convergence_duration), bytes + got, sizeof(pkt->convergence_duration));
    //got += sizeof(pkt->convergence_duration);
    memcpy(&(pkt->flags), bytes + got, sizeof(pkt->flags));
    got += sizeof(pkt->flags);
    memcpy(&(pkt->stream_index), bytes + got, sizeof(pkt->stream_index));
    got += sizeof(pkt->stream_index);

    // ts
    if(ts != nullptr) {
        memcpy(ts, bytes + got, sizeof(long long));
    }
    got += sizeof(long long);

    int wholeSize = 0;
    memcpy(&wholeSize, bytes + got, sizeof(wholeSize));
    got += sizeof(wholeSize);
    got += 8;
    //spdlog::debug("wholeSize: {:d}, {:d}", wholeSize, got);

    return ret;
}
} // namespace AVPacketSerializer

void mqPacketFree(void *data, void *hint)
{
    free(data);
}

// AVFormatCtxSerializer
namespace AVFormatCtxSerializer
{
/**
 * memory layerout
 * PS_MARK_S | NUM_STREAMS | AVSTREAM+AVCODEPAR | WHOLESIZE | PS_MARK_E
 * */


int encode(AVFormatContext *ctx, char **bytes, vector<int> ids = vector<int>())
{
    int wholeSize = 0;
    int got = 0;
    // calc total size
    wholeSize += strlen(PS_MARK_S);
    // num streams
    wholeSize += sizeof(ctx->nb_streams);
    
    int numStreams = ctx->nb_streams;
    if(ids.size() != 0) {
      numStreams = ids.size();
    }else{
      for(int i = 0; i < numStreams; i++){
        ids.push_back(i);
      }
    }

    spdlog::info("encode num of streams: {}, {}", ctx->nb_streams, numStreams);
    for(auto i: ids){
        spdlog::info("\t sid: {}", i);
    }

    for (auto i:ids)
    {
        wholeSize += sizeof(AVStream);
        wholeSize += sizeof(AVCodecParameters);
        //extradata
        wholeSize += sizeof(ctx->streams[i]->codecpar->extradata_size);
        if(ctx->streams[i]->codecpar->extradata_size!=0){
            wholeSize += ctx->streams[i]->codecpar->extradata_size;
        }
    }
    wholeSize += sizeof(wholeSize);
    wholeSize += strlen(PS_MARK_E);

    // alloc memory
    *bytes = (char *)malloc(wholeSize);
    // populate
    memcpy((*bytes) + got, PS_MARK_S, strlen(PS_MARK_S));
    got += strlen(PS_MARK_S);
    memcpy((*bytes) + got, (void *)&(numStreams), sizeof(ctx->nb_streams));
    got += sizeof(ctx->nb_streams);
    for (auto i: ids)
    {
        //
        memcpy((*bytes) + got, ctx->streams[i], sizeof(AVStream));
        got += sizeof(AVStream);
        //
        memcpy((*bytes) + got, ctx->streams[i]->codecpar, sizeof(AVCodecParameters));
        got += sizeof(AVCodecParameters);
        //extra
        memcpy((*bytes) + got, &(ctx->streams[i]->codecpar->extradata_size), sizeof(ctx->streams[i]->codecpar->extradata_size));
        got += sizeof(ctx->streams[i]->codecpar->extradata_size);
        memcpy((*bytes) + got,ctx->streams[i]->codecpar->extradata, ctx->streams[i]->codecpar->extradata_size);
        got += ctx->streams[i]->codecpar->extradata_size;
    }
    memcpy((*bytes) + got, &wholeSize, sizeof(wholeSize));
    got += sizeof(wholeSize);
    memcpy((*bytes) + got, PS_MARK_E, strlen(PS_MARK_E));
    got += strlen(PS_MARK_E);
    if(wholeSize != got){
        spdlog::error("avctx encode wholesize: {}, should be {}", got, wholeSize);
    }
    assert(wholeSize == got);
    
    return wholeSize;
}

int decode(char *bytes, int len, AVFormatContext *pCtx)
{
    int ret = 0;
    int got = 0;
    memcpy(&ret, bytes +len -strlen(PS_MARK_E) - sizeof(ret), sizeof(ret));
    if ((memcmp(PS_MARK_S, bytes + got, strlen(PS_MARK_S)) != 0 && memcmp(PS_MARK_E, bytes + len - strlen(PS_MARK_E), strlen(PS_MARK_E)) != 0)||ret != len)
    {
        spdlog::error("invalid avformatctx: {} {}", ret, len);
        return -1;
    }
    spdlog::debug("decode len: {}", ret);
    got += strlen(PS_MARK_S);
    memcpy(&ret, bytes + got, sizeof(ret));
    got += sizeof(ret);
    pCtx->streams = (AVStream **)av_malloc(sizeof(AVStream *) * ret);
    pCtx->nb_streams = ret;
    spdlog::debug("avctx decode streams:{}", ret);
    int numExtra = ret;
    for (int i = 0; i < numExtra; i++)
    {
        ret = 0;
        pCtx->streams[i] = (AVStream *)av_malloc(sizeof(AVStream));
        memcpy(pCtx->streams[i], bytes + got, sizeof(AVStream));
        got += sizeof(AVStream);
        pCtx->streams[i]->codecpar = (AVCodecParameters *)malloc(sizeof(AVCodecParameters));
        memcpy(pCtx->streams[i]->codecpar, bytes + got, sizeof(AVCodecParameters));
        got += sizeof(AVCodecParameters);
        // extra
        memcpy(&ret, bytes + got, sizeof(int));
        got += sizeof(int);
        if(ret != 0) {
            spdlog::debug("avctx decode has extra : {}", ret);
            pCtx->streams[i]->codecpar->extradata_size = ret;
            pCtx->streams[i]->codecpar->extradata = (uint8_t *)av_malloc(ret);
            memcpy(pCtx->streams[i]->codecpar->extradata, bytes + got, ret);
            got += ret;
        }
    }
    memcpy(&ret, bytes + got, sizeof(ret));
    got += sizeof(ret);
    got += strlen(PS_MARK_E);
    if(len != ret) {
        spdlog::error("avformatctx decode: {:d} {:d} {:d}", ret, len, got);
    }
    
    assert(ret == len);
    return ret;
}

void freeCtx(AVFormatContext *pCtx)
{
    for (int i = 0; i < pCtx->nb_streams; i++)
    {
        free(pCtx->streams[i]->codecpar);
        if(pCtx->streams[i]->codecpar->extradata_size != 0) {
            free(pCtx->streams[i]->codecpar->extradata);
        }
        free(pCtx->streams[i]);
    }
    free(pCtx->streams);
}
} // namespace AVFormatCtxSerializer

#endif