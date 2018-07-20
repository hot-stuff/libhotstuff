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
using salticidae::MsgNetwork;

using hotstuff::ReplicaID;
using hotstuff::NetAddr;
using hotstuff::EventContext;
using hotstuff::uint256_t;
using hotstuff::MsgClient;
using hotstuff::CommandDummy;
using hotstuff::command_t;
using hotstuff::Finality;
using hotstuff::HotStuffError;

EventContext eb;
ReplicaID proposer;
size_t max_async_num;
int max_iter_num;

struct Request {
    ReplicaID rid;
    command_t cmd;
    ElapsedTime et;
    Request(ReplicaID rid, const command_t &cmd):
        rid(rid), cmd(cmd) { et.start(); }
};

std::unordered_map<ReplicaID, MsgNetwork<MsgClient>::conn_t> conns;
std::unordered_map<const uint256_t, Request> waiting;
std::vector<NetAddr> replicas;
MsgNetwork<MsgClient> mn(eb, 10, 10, 4096);

void set_proposer(ReplicaID rid) {
    proposer = rid;
    auto it = conns.find(rid);
    if (it == conns.end())
        conns.insert(std::make_pair(rid, mn.connect(replicas[rid])));
}

void try_send() {
    while (waiting.size() < max_async_num && max_iter_num)
    {
        auto cmd = CommandDummy::make_cmd();
        MsgClient msg;
        msg.gen_reqcmd(*cmd);
        mn.send_msg(msg, conns.find(proposer)->second);
        HOTSTUFF_LOG_INFO("send new cmd %.10s",
                            get_hex(cmd->get_hash()).c_str());
        waiting.insert(std::make_pair(
            cmd->get_hash(), Request(proposer, cmd)));
        if (max_iter_num > 0)
            max_iter_num--;
    }
}

void on_receive(const MsgClient &msg, MsgNetwork<MsgClient>::conn_t) {
    Finality fin;
    HOTSTUFF_LOG_DEBUG("got %s", std::string(msg).c_str());
    if (!msg.verify_checksum())
        HOTSTUFF_LOG_ERROR("incorrect checksum %08x", msg.get_checksum());
    msg.parse_respcmd(fin);
    const uint256_t &cmd_hash = fin.cmd_hash;
    auto it = waiting.find(cmd_hash);
    if (fin.rid != proposer)
    {
        HOTSTUFF_LOG_INFO("reconnect to the new proposer");
        set_proposer(fin.rid);
    }
    if (fin.rid != it->second.rid)
    {
        MsgClient msg;
        msg.gen_reqcmd(*(waiting.find(cmd_hash)->second.cmd));
        mn.send_msg(msg, conns.find(proposer)->second);
        HOTSTUFF_LOG_INFO("resend cmd %.10s",
                            get_hex(cmd_hash).c_str());
        it->second.et.start();
        it->second.rid = proposer;
        return;
    }
    HOTSTUFF_LOG_INFO("got %s", std::string(fin).c_str());
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
    auto opt_max_iter_num = Config::OptValInt::create(100);
    auto opt_max_async_num = Config::OptValInt::create(10);

    mn.reg_handler(hotstuff::RESP_CMD, on_receive);

    try {
        config.add_opt("idx", opt_idx, Config::SET_VAL);
        config.add_opt("replica", opt_replicas, Config::APPEND);
        config.add_opt("iter", opt_max_iter_num, Config::SET_VAL);
        config.add_opt("max-async", opt_max_async_num, Config::SET_VAL);
        config.parse(argc, argv);
        auto idx = opt_idx->get();
        max_iter_num = opt_max_iter_num->get();
        max_async_num = opt_max_async_num->get();
        std::vector<std::pair<std::string, std::string>> raw;
        for (const auto &s: opt_replicas->get())
        {
            auto res = trim_all(split(s, ","));
            if (res.size() != 2)
                throw HotStuffError("format error");
            raw.push_back(std::make_pair(res[0], res[1]));
        }

        if (!(0 <= idx && (size_t)idx < raw.size() && raw.size() > 0))
            throw std::invalid_argument("out of range");
        for (const auto &p: raw)
        {
            auto _p = split_ip_port_cport(p.first);
            size_t _;
            replicas.push_back(NetAddr(NetAddr(_p.first).ip, htons(stoi(_p.second, &_))));
        }

        set_proposer(idx);
        try_send();
        eb.dispatch();
    } catch (HotStuffError &e) {
        HOTSTUFF_LOG_ERROR("exception: %s", std::string(e).c_str());
    }
    return 0;
}
