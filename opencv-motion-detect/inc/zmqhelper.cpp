#include "zmqhelper.hpp"

namespace zmqhelper {

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
vector<vector<uint8_t> > z_recv_multiple(void *s, bool nowait)
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

int z_send(void *s, string peerId, string selfId, const json &meta, string body)
{
    vector<vector<uint8_t> > v{str2body(peerId), str2body(selfId), str2body(meta.dump()), str2body(body)};
    return z_send_multiple(s, v);
}

int z_send(void *s, string peerId, string selfId,vector<uint8_t> meta, vector<uint8_t> body)
{
    vector<vector<uint8_t> > v{str2body(peerId), str2body(selfId), meta, body};
    return z_send_multiple(s, v);
}

int z_send(void *s, string peerId, string meta, string body)
{
    vector<vector<uint8_t> > v{str2body(peerId), str2body(meta), str2body(body)};
    return z_send_multiple(s, v);
}

int z_send(void *s, string peerId, vector<uint8_t> meta, vector<uint8_t> body)
{
    vector<vector<uint8_t> > v{str2body(peerId), meta, body};
    return z_send_multiple(s, v);
}

/// setup router
int setupRouter(void **ctx, void **s, string addr){
    int ret = 0;
    int opt_notify = ZMQ_NOTIFY_DISCONNECT|ZMQ_NOTIFY_CONNECT;
    *ctx = zmq_ctx_new();
    *s = zmq_socket(*ctx, ZMQ_ROUTER);
    //ZMQ_TCP_KEEPALIVE
    //ZMQ_TCP_KEEPALIVE_IDLE
    //ZMQ_TCP_KEEPALIVE_INTVL
    ret = 1;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
    ret = 5;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
    ret = 2;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_CNT, &ret, sizeof (ret));
    zmq_setsockopt(*s, ZMQ_ROUTER_NOTIFY, &opt_notify, sizeof (opt_notify));
    ret = zmq_bind(*s, addr.c_str());
    if(ret < 0) {
        spdlog::debug("failed to bind zmq at {} for reason: {}, retrying load configuration...", addr, zmq_strerror(zmq_errno()));
    }
    return ret;
}

/// setup dealer
/// @return 0 success, otherwise failed
int setupDealer(void **ctx, void **s, string addr, string ident, int timeout) {
    int ret = 0;
    *ctx = zmq_ctx_new();
    *s = zmq_socket(*ctx, ZMQ_DEALER);
    ret = 1;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE, &ret, sizeof (ret));
    ret = 20;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_IDLE, &ret, sizeof (ret));
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_INTVL, &ret, sizeof (ret));
    ret = 2;
    zmq_setsockopt(*s, ZMQ_TCP_KEEPALIVE_CNT, &ret, sizeof (ret));
    if(timeout != 0) {
        if(timeout == -1) {
            timeout = 10*1000;
        }
        zmq_setsockopt(*s, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    }
    
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

/// recv config msg:
/// @return 0 success, otherwise failed.
int recvConfigMsg(void *s, json &config, string addr, string ident){
    bool bConfigGot = false;
    while(!bConfigGot){
        auto v = zmqhelper::z_recv_multiple(s);
        if(v.size() != 3) {
            spdlog::error("{} invalid msg from daemon: {}", ident, addr);
            return -1;
        }

        spdlog::debug("{} configuration msg received: {} {} {}", ident, body2str(v[0]), body2str(v[1]), body2str(v[2]));
        try{
            string sMeta = json::parse(body2str(v[1]))["type"];
            if(sMeta != EV_MSG_META_CONFIG) {
                throw StrException("meta type is:" + sMeta + ", but expecting " + EV_MSG_META_CONFIG);
            }
            config = json::parse(body2str(v[2]));
            bConfigGot = true;
        }catch(exception &e) {
            spdlog::error("{} invalid config msg from daemon {}, {}", ident, addr, e.what());
            return -1;
        }
    }

    return 0;
}


int forkSubsystem(string devSn, string peerId, int drPort, pid_t &pid){
    int ret = 0;
    auto v = strutils::split(peerId, ':');
    string modName = v[1];
    string sn = v[0];
    if( (pid = fork()) == -1 ) {
        spdlog::error("evdamon {} failed to fork subsytem - evmgr", devSn);
        return -1;
    }else if(pid == 0) {
        ret += setenv("PEERID", peerId.c_str(), 1);
        ret += setenv("DR_PORT", to_string(drPort).c_str(), 1);
        if(ret < 0) {
            spdlog::error("evdaemon {} failed to set env", devSn);
            return -2;
        }
        execl((string("./") + modName).c_str(), NULL, NULL, NULL);
        spdlog::error("evdaemon {} failed to startup evmgr", devSn);
    }else{
        // parent
    }

    return 0;
}


}