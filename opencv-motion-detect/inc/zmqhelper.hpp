/*
module: zmqhelper
description: 
author: Bruce.Lu <lzbgt@icloud.com>
update: 2019/08/23
*/

#ifndef __ZMQ_HELPER_H__
#define __ZMQ_HELPER_H__

#undef ZMQ_BUILD_DRAFT_API
#define ZMQ_BUILD_DRAFT_API 1

#include "zmq.h"
#include <vector>
#include <spdlog/spdlog.h>

using namespace std;

namespace zmqhelper {
#define EV_HEARTBEAT_SECONDS 30
#define MSG_HELLO "hello"
#define EV_MSG_META_PING "ping"
#define EV_MSG_META_PONG "pong"
#define EV_MSG_META_EVENT "event"
#define EV_MSG_META_CMD "cmd"
#define EV_MSG_META_CONFIG "config"
#define EV_MSG_META_AVFORMATCTX "afctx"

#define EV_MSG_TYPE_AI_MOTION "ai_motion"
#define EV_MSG_TYPE_CONN_STAT "connstat"
#define EV_MSG_TYPE_SYS_STAT "sysstat"
// #define EV_MSG_CMD_RESTART "restart"
// #define EV_MSG_CMD_UPDATE "update"

#define EV_MSG_EVENT_MOTION_START "start"
#define EV_MSG_EVENT_MOTION_END "end"
#define EV_MSG_EVENT_CONN_CONN "connect"
#define EV_MSG_EVENT_CONN_DISCONN "disconnect"

#define EV_NUM_CACHE_PERPEER 100
#define MAX_EVENT_QUEUE_SIZE 50

//
string body2str(vector<uint8_t> body)
{
    return string((char *)(body.data()), body.size());
}

vector<uint8_t> data2body(char* data, int len)
{
    vector<uint8_t> v;
    v.insert(v.end(), (uint8_t *)data, (uint8_t *)data+len);
    return v;
}

vector<uint8_t> str2body(string const &str)
{
    vector<uint8_t> v;
    v.insert(v.end(), (uint8_t*)(str.data()), (uint8_t *)(str.data()) + str.size());
    return v;
}

// proto: 1. on router [sender_id] [target_id] [body]
//        2. on dealer [sender_id] [body]
vector<vector<uint8_t> > z_recv_multiple(void *s, bool nowait=false)
{
    int64_t more = 1;
    vector<vector<uint8_t> > body;
    int cnt = 0;
    int ret = 0;
    while(more > 0) {
        cnt++;
        zmq_msg_t msg;
        ret = zmq_msg_init(&msg);
        if(ret < 0) {
            spdlog::debug("failed to receive multiple msg on zmq_msg_init: {}", zmq_strerror(zmq_errno()));
            break;
        }
        ret = zmq_recvmsg(s, &msg, nowait?ZMQ_DONTWAIT:0);
        if(ret < 0) {
            spdlog::debug("z_recv_multiple: {}", zmq_strerror(zmq_errno()));
            break;
        }
        
        vector<uint8_t> v;
        v.insert(v.end(), (uint8_t*)zmq_msg_data(&msg), (uint8_t*)zmq_msg_data(&msg)+ret);
        body.push_back(v);
        spdlog::debug("z_rcv_multiple: {}", body2str(v).substr(0, v.size()> 100? 15:v.size()));
        zmq_msg_close(&msg);
        size_t more_size = sizeof(more);
        ret = zmq_getsockopt(s, ZMQ_RCVMORE, &more, &more_size);
        if(ret < 0) {
            spdlog::debug("z_recv_multiple: {}", zmq_strerror(zmq_errno()));
            break;
        }
    }

    return body;
}

// proto [sender_id(only when no identifier set in setsockopts)] [target_id] [body]
int z_send_multiple(void *s, vector<vector<uint8_t> >&body)
{
    size_t cnt = 0;
    int ret = 0;
    zmq_msg_t msg;
    for(auto &i:body) {
        ret = zmq_msg_init_size(&msg, i.size());
        memcpy(zmq_msg_data(&msg), (void*)(i.data()), i.size());
        spdlog::debug("z_send_multiple: {}", body2str(i).substr(0, i.size()>100?15:i.size()));
        if(ret < 0) {
            spdlog::debug("z_send_multiple: {}", zmq_strerror(zmq_errno()));
            break;
        }
        ret = zmq_msg_send(&msg, s, cnt==(body.size()-1)?0:(ZMQ_SNDMORE));
        zmq_msg_close(&msg);
        if(ret < 0) {
            spdlog::debug("z_send_multiple: {}", zmq_strerror(zmq_errno()));
            break;
        }
        cnt++;
    }
    return ret;
}

/// setup router
int setupRouter(void **ctx, void **s, string addr){
    int ret = 0;
    int opt_notify = ZMQ_NOTIFY_DISCONNECT|ZMQ_NOTIFY_CONNECT;
    *ctx = zmq_ctx_new();
    *s = zmq_socket(*ctx, ZMQ_ROUTER);
    zmq_setsockopt (*s, ZMQ_ROUTER_NOTIFY, &opt_notify, sizeof (opt_notify));
    ret = zmq_bind(*s, addr.c_str());
    if(ret < 0) {
        spdlog::debug("failed to bind zmq at {} for reason: {}, retrying load configuration...", addr, zmq_strerror(zmq_errno()));
    }
    return ret;
}

/// setup dealer
int setupDealer(void **ctx, void **s, string addr, string ident) {
    int ret = 0;
    *ctx = zmq_ctx_new();
    *s = zmq_socket(*ctx, ZMQ_DEALER);
    ret = zmq_setsockopt(*s, ZMQ_IDENTITY, ident.c_str(), ident.size());
    ret += zmq_setsockopt (*s, ZMQ_ROUTING_ID, ident.c_str(), ident.size());
    if(ret < 0) {
        spdlog::debug("{} failed setsockopts ZMQ_ROUTING_ID to {}: {}", ident, addr, zmq_strerror(zmq_errno()));
    }else{
        ret = zmq_connect(*s, addr.c_str());
            if(ret != 0) {
                spdlog::error("{} failed connect dealer: {}", ident, addr);
            }
    }

    return ret;  
}



}



#endif