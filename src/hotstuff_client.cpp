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

using hotstuff::ReplicaID;
using hotstuff::NetAddr;
using hotstuff::EventContext;
using hotstuff::Event;
using hotstuff::uint256_t;
using hotstuff::bytearray_t;
using hotstuff::MsgClient;
using hotstuff::CommandDummy;
using hotstuff::command_t;
using hotstuff::Finality;

EventContext eb;
size_t max_async_num = 10;
int max_iter_num = 100;
ReplicaID proposer;

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

void on_receive(int);

struct Request {
    ReplicaID rid;
    command_t cmd;
    ElapsedTime et;
    Request(ReplicaID rid, const command_t &cmd):
        rid(rid), cmd(cmd) { et.start(); }
};

struct Conn {
    int fd;
    Event on_receive_ev;

    Conn(const NetAddr &addr):
        fd(connect(addr)),
        on_receive_ev(eb, fd, EV_READ, [this](int fd, short) {
            on_receive(fd);
            on_receive_ev.add();
        }) { on_receive_ev.add(); }

    Conn(Conn &&other):
            fd(other.fd),
            on_receive_ev(eb, fd, EV_READ, [this](int fd, short) {
                on_receive(fd);
                on_receive_ev.add();
            }) { 
        other.fd = -1;
        other.on_receive_ev.del();
        on_receive_ev.add();
    }

    ~Conn() { if (fd != -1) close(fd); }
};

std::unordered_map<int, salticidae::RingBuffer> buffers;
std::unordered_map<const uint256_t, Request> waiting;
std::unordered_map<ReplicaID, Conn> conns;
std::vector<NetAddr> replicas;


void setup(ReplicaID rid) {
    proposer = rid;
    auto it = conns.find(rid);
    if (it == conns.end())
        conns.insert(std::make_pair(rid, Conn(replicas[rid])));
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

void try_send() {
    while (waiting.size() < max_async_num && max_iter_num)
    {
        auto cmd = CommandDummy::make_cmd();
        MsgClient msg;
        msg.gen_reqcmd(*cmd);
        write_msg(conns.find(proposer)->second.fd, msg);
        HOTSTUFF_LOG_INFO("send new cmd %.10s",
                            get_hex(cmd->get_hash()).c_str());
        waiting.insert(std::make_pair(
            cmd->get_hash(), Request(proposer, cmd)));
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
    auto it = waiting.find(cmd_hash);
    if (fin.rid != proposer)
    {
        HOTSTUFF_LOG_INFO("reconnect to the new proposer");
        setup(fin.rid);
    }
    if (fin.rid != it->second.rid)
    {
        MsgClient msg;
        msg.gen_reqcmd(*(waiting.find(cmd_hash)->second.cmd));
        write_msg(conns.find(proposer)->second.fd, msg);
        HOTSTUFF_LOG_INFO("resend cmd %.10s",
                            get_hex(cmd_hash).c_str());
        it->second.et.start();
        it->second.rid = proposer;
        return;
    }
    HOTSTUFF_LOG_INFO(
        "fd %d got response for %.10s: <decision=%d, blk=%.10s>",
        fd, get_hex(cmd_hash).c_str(),
        fin.decision,
        get_hex(fin.blk_hash).c_str());
    if (it == waiting.end()) return;
    waiting.erase(it);
    try_send();
}

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = trim_all(split(s, ";"));
    return std::make_pair(ret[0], ret[1]);
}


int main(int argc, char **argv) {
    Config config("hotstuff.conf");
    auto opt_idx = Config::OptValInt::create(0);
    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_max_iter_num = Config::OptValInt::create();

    try {
        config.add_opt("idx", opt_idx, Config::SET_VAL);
        config.add_opt("replica", opt_replicas, Config::APPEND);
        config.add_opt("ntx", opt_max_iter_num, Config::SET_VAL);
        config.parse(argc, argv);
        auto idx = opt_idx->get();
        max_iter_num = opt_max_iter_num->get();
        std::vector<std::pair<std::string, std::string>> raw;
        for (const auto &s: opt_replicas->get())
        {
            auto res = trim_all(split(s, ","));
            assert(res.size() == 2);
            raw.push_back(std::make_pair(res[0], res[1]));
        }

        if (!(0 <= idx && (size_t)idx < raw.size() &&
            raw.size() > 0))
            throw std::invalid_argument("out of range");
        for (const auto &p: raw)
        {
            auto _p = split_ip_port_cport(p.first);
            size_t _;
            replicas.push_back(NetAddr(NetAddr(_p.first).ip, htons(stoi(_p.second, &_))));
        }

        setup(idx);
        try_send();
        eb.dispatch();
    } catch (hotstuff::HotStuffError &e) {
        HOTSTUFF_LOG_ERROR("exception: %s", std::string(e).c_str());
    }
    return 0;
}
