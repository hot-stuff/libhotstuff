#include <cassert>
#include "salticidae/type.h"
#include "salticidae/netaddr.h"
#include "salticidae/network.h"
#include "salticidae/util.h"

#include "hotstuff/util.h"
#include "hotstuff/type.h"
#include "hotstuff/client.h"

using salticidae::Config;
using salticidae::ElapsedTime;
using salticidae::trim_all;
using salticidae::split;

using hotstuff::NetAddr;
using hotstuff::EventContext;
using hotstuff::Event;
using hotstuff::uint256_t;
using hotstuff::bytearray_t;
using hotstuff::MsgClient;
using hotstuff::CommandDummy;
using hotstuff::Finality;

size_t max_async_num = 10;
int max_iter_num = 100;

struct Request {
    ElapsedTime et;
    Request() { et.start(); }
};

std::unordered_map<int, salticidae::RingBuffer> buffers;
std::unordered_map<const uint256_t, Request> waiting;

int connect(const NetAddr &node) {
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        assert(0);
    struct sockaddr_in sockin;
    memset(&sockin, 0, sizeof(struct sockaddr_in));
    sockin.sin_family = AF_INET;
    sockin.sin_addr.s_addr = node.ip;
    sockin.sin_port = node.port;
    if (connect(fd, (struct sockaddr *)&sockin, sizeof(struct sockaddr_in)) == -1)
        assert(0);
    HOTSTUFF_LOG_INFO("connected to %s", std::string(node).c_str());
    return fd;
}

void write_msg(int fd, const MsgClient &msg) {
    bytearray_t msg_data = msg.serialize();
    if (write(fd, msg_data.data(), msg_data.size()) != (ssize_t)msg_data.size())
        assert(0);
}

void read_msg(int fd, MsgClient &msg) {
    static const size_t BUFF_SEG_SIZE = 1024;
    ssize_t ret;
    auto &buffer = buffers[fd];
    bool read_body = false;
    for (;;)
    {
        bytearray_t buff_seg;
        if (!read_body && buffer.size() >= MsgClient::header_size)
        {
            buff_seg = buffer.pop(MsgClient::header_size);
            msg = MsgClient(buff_seg.data());
            read_body = true;
        }
        if (read_body && buffer.size() >= msg.get_length())
        {
            buff_seg = buffer.pop(msg.get_length());
            msg.set_payload(std::move(buff_seg));
            return;
        }

        buff_seg.resize(BUFF_SEG_SIZE);
        ret = read(fd, buff_seg.data(), BUFF_SEG_SIZE);
        assert(ret != -1);
        if (ret > 0)
        {
            buff_seg.resize(ret);
            buffer.push(std::move(buff_seg));
        }
    }
}

void try_send(int fd) {
    while (waiting.size() < max_async_num && max_iter_num)
    {
        auto cmd = CommandDummy::make_cmd();
        MsgClient msg;
        msg.gen_reqcmd(*cmd);
        write_msg(fd, msg);
        HOTSTUFF_LOG_INFO("send new cmd %.10s",
                            get_hex(cmd->get_hash()).c_str());
        waiting.insert(std::make_pair(
            cmd->get_hash(), Request()));
        if (max_iter_num > 0)
            max_iter_num--;
    }
}

void on_receive(int fd) {
    MsgClient msg;
    uint256_t cmd_hash;
    Finality fin;
    read_msg(fd, msg);
    HOTSTUFF_LOG_DEBUG("got %s", std::string(msg).c_str());
    if (!msg.verify_checksum())
        HOTSTUFF_LOG_ERROR("incorrect checksum %08x", msg.get_checksum());
    msg.parse_respcmd(cmd_hash, fin);
    HOTSTUFF_LOG_INFO(
        "fd %d got response for %.10s: <decision=%d, blk=%.10s>",
        fd, get_hex(cmd_hash).c_str(),
        fin.decision,
        get_hex(fin.blk_hash).c_str());
    auto it = waiting.find(cmd_hash);
    if (it == waiting.end()) return;
    waiting.erase(it);
    try_send(fd);
}

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = trim_all(split(s, ";"));
    return std::make_pair(ret[0], ret[1]);
}

Event *on_receive_ev;

int main(int argc, char **argv) {
    Config config("hotstuff.conf");
    std::vector<NetAddr> peers2;
    EventContext eb;
    auto opt_idx = Config::OptValInt::create(-1);
    auto opt_server_addr = Config::OptValStr::create("127.0.0.1:2234");
    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_max_iter_num = Config::OptValInt::create();

    try {
        config.add_opt("idx", opt_idx, Config::SET_VAL);
        config.add_opt("server", opt_server_addr, Config::SET_VAL);
        config.add_opt("replica", opt_replicas, Config::APPEND);
        config.add_opt("ntx", opt_max_iter_num, Config::SET_VAL);
        config.parse(argc, argv);
        auto idx = opt_idx->get();
        max_iter_num = opt_max_iter_num->get();
        std::vector<std::pair<std::string, std::string>> replicas;
        for (const auto &s: opt_replicas->get())
        {
            auto res = trim_all(split(s, ","));
            assert(res.size() == 2);
            replicas.push_back(std::make_pair(res[0], res[1]));
        }

        NetAddr server(opt_server_addr->get());
        if (-1 < idx && (size_t)idx < replicas.size() &&
            replicas.size() > 0)
        {
            for (const auto &p: replicas)
            {
                auto _p = split_ip_port_cport(p.first);
                size_t _;
                peers2.push_back(NetAddr(NetAddr(_p.first).ip, htons(stoi(_p.second, &_))));
            }
            server = peers2[idx];
        }

        int fd = connect(server);
        on_receive_ev = new Event{eb, fd, EV_READ, [](int fd, short) {
            on_receive(fd);
            on_receive_ev->add();
        }};
        on_receive_ev->add();
        try_send(fd);
        eb.dispatch();
    } catch (hotstuff::HotStuffError &e) {
        HOTSTUFF_LOG_ERROR("exception: %s", std::string(e).c_str());
    }
    return 0;
}
