#ifndef __EV_REVERSE_TUN__
#define __EV_REVERSE_TUN__

#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <string>
#include <thread>
#include <spdlog/spdlog.h>
#include <fmt/format.h>


using namespace std;

#ifndef INADDR_NONE
#define INADDR_NONE (in_addr_t)-1
#endif

const char *keyfile1 = "/home/username/.ssh/id_rsa.pub";
const char *keyfile2 = "/home/username/.ssh/id_rsa";

enum {
    AUTH_NONE = 0,
    AUTH_PASSWORD,
    AUTH_PUBLICKEY
};


int createReverseTun(string host, int port, string user, string _password, thread &thWorker)
{
    int remote_listenport = -1;
    const char *server_ip = host.c_str();
    const char *remote_listenhost = "0.0.0.0"; /* resolved by the server */
    int remote_wantport = port;
    const char *local_destip = "127.0.0.1";
    int local_destport = 22;
    const char *username = user.c_str();
    const char *password = _password.c_str();
    int rc, i, auth = AUTH_PASSWORD;
    struct sockaddr_in sin;
    socklen_t sinlen = sizeof(sin);
    const char *fingerprint;
    char *userauthlist;
    LIBSSH2_SESSION *session = nullptr;
    LIBSSH2_LISTENER *listener = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    fd_set fds;
    struct timeval tv;
    ssize_t len, wr;
    char buf[10240]; // 10K
    string msg = "Fingerprint: ";
    int sock = -1, forwardsock = -1;

    auto closeFun = [&](){
        if(forwardsock)
            close(forwardsock);
        if(channel)
            libssh2_channel_free(channel);
        if(listener)
            libssh2_channel_forward_cancel(listener);

        if(session) {
            libssh2_session_disconnect(session, "Client disconnecting normally");
            libssh2_session_free(session);
        }
        if(sock) close(sock);
        libssh2_exit();
    };

    rc = libssh2_init(0);

    if(rc != 0) {
        spdlog::error("tun failed libssh2 initialization {}", rc);
        return -1;
    }

    /* Connect to SSH server */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sock == -1) {
        spdlog::error("tun failed create socket");
        closeFun(); return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(server_ip);
    if(INADDR_NONE == sin.sin_addr.s_addr) {
        spdlog::error("tun failed get inet_addr");
        closeFun(); return -1;
    }
    sin.sin_port = htons(22);
    if(connect(sock, (struct sockaddr*)(&sin),
               sizeof(struct sockaddr_in)) != 0) {
        spdlog::error("tun failed to connect server {}", server_ip);
        closeFun(); return -1;
    }

    /* Create a session instance */
    session = libssh2_session_init();
    if(!session) {
        spdlog::error("tun failed to initialize ssh session");
        closeFun(); return -1;
    }

    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */
    rc = libssh2_session_handshake(session, sock);
    if(rc) {
        spdlog::error("failed to handshake up SSH session: %d\n", rc);
        closeFun(); return -1;
    }

    /* At this point we havn't yet authenticated.  The first thing to do
     * is check the hostkey's fingerprint against our known hosts Your app
     * may have it hard coded, may go to a file, may present it to the
     * user, that's your call
     */
    fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

    for(i = 0; i < 20; i++)
        msg += fmt::format("{:02x} ", (unsigned char)fingerprint[i]);
    spdlog::info(msg);

    /* check what authentication methods are available */
    userauthlist = libssh2_userauth_list(session, username, strlen(username));

    if(strstr(userauthlist, "password"))
        auth |= AUTH_PASSWORD;
    if(strstr(userauthlist, "publickey"))
        auth |= AUTH_PUBLICKEY;

    if(auth & AUTH_PASSWORD) {
        if(libssh2_userauth_password(session, username, password)) {
            spdlog::error("tun authentication by password failed.");
            closeFun(); return -1;
        }
    }
    else if(auth & AUTH_PUBLICKEY) {
        if(libssh2_userauth_publickey_fromfile(session, username, keyfile1,
                                               keyfile2, password)) {
            spdlog::error("tun authentication by public key failed");
            closeFun(); return -1;
        }
        spdlog::info("tun Authentication by public key succeeded");
    }
    else {
        spdlog::error("tun No supported authentication methods found");
        closeFun(); return -1;
    }

    spdlog::info("tun asking server to listen on remote{}:{}",
                 remote_listenhost, remote_wantport);

    listener = libssh2_channel_forward_listen_ex(session, remote_listenhost,

               remote_wantport, &remote_listenport, 1);
    if(!listener) {
        spdlog::error("tun Could not start the tcpip-forward listener."
                      "(Note that this can be a problem at the server!"
                      " Please review the server logs.)");
        closeFun(); return -1;
    }

    spdlog::info("tun server is listening on {}:{}", remote_listenhost,
                 remote_listenport);

    spdlog::info("tun waiting for remote connection");
    channel = libssh2_channel_forward_accept(listener);

    if(!channel) {
        spdlog::error("tun could not accept connection!\n"
                      "(Note that this can be a problem at the server!"
                      " Please review the server logs.)\n");
        closeFun(); return -1;
    }

    spdlog::info(
        "tun accepted remote connection. connecting to local server {}:{}",
        local_destip, local_destport);
    forwardsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(forwardsock == -1) {
        spdlog::error("tun failed to create reverse socket");
        closeFun(); return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(local_destport);
    sin.sin_addr.s_addr = inet_addr(local_destip);
    if(INADDR_NONE == sin.sin_addr.s_addr) {
        spdlog::error("tun failed to get reverse inet_addr");
        closeFun(); return -1;
    }
    if(-1 == connect(forwardsock, (struct sockaddr *)&sin, sinlen)) {
        spdlog::error("tun failed to connect reverse socks");
        closeFun(); return -1;
    }

    spdlog::info("tun orwarding connection from remote %s:%d to local %s:%d\n",
                 remote_listenhost, remote_listenport, local_destip, local_destport);

    /* Must use non-blocking IO hereafter due to the current libssh2 API */
    libssh2_session_set_blocking(session, 0);

    thWorker = thread([&]() {
        bool hasError = false;
        while(1) {
            FD_ZERO(&fds);
            FD_SET(forwardsock, &fds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            rc = select(forwardsock + 1, &fds, nullptr, nullptr, &tv);
            if(-1 == rc) {
                spdlog::error("tun failed to select");
                break;
            }

            if(rc && FD_ISSET(forwardsock, &fds)) {
                len = recv(forwardsock, buf, sizeof(buf), 0);
                if(len < 0) {
                    spdlog::error("tun failed to read");
                    break;
                }
                else if(0 == len) {
                    spdlog::info("tun the local server at {}:{} disconnected!\n",
                                 local_destip, local_destport);
                    break;
                }
                wr = 0;
                do {
                    i = libssh2_channel_write(channel, buf, len);
                    if(i < 0) {
                        spdlog::error("tun failed to libssh2_channel_write: {}", i);
                        hasError = true;
                        break;
                    }
                    wr += i;
                }
                while(i > 0 && wr < len);
            }

            while(1) {
                len = libssh2_channel_read(channel, buf, sizeof(buf));
                if(LIBSSH2_ERROR_EAGAIN == len)
                    break;
                else if(len < 0) {
                    spdlog::error("tun failed libssh2_channel_read: {}", (int)len);
                    hasError = true;
                    break;
                }
                wr = 0;
                while(wr < len) {
                    i = send(forwardsock, buf + wr, len - wr, 0);
                    if(i <= 0) {
                        spdlog::error("tun failed to reverse write");
                        hasError = true;
                        break;
                    }
                    wr += i;
                }

                if(libssh2_channel_eof(channel)) {
                    spdlog::error("tun the remote client at {}:{} disconnected!",
                                  remote_listenhost, remote_listenport);
                    break;
                }
            }
        }

        closeFun();
    });

    return remote_listenport;
}

#endif