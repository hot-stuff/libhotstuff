#include <iostream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <random>
#include <unistd.h>
#include <signal.h>
#include <event2/event.h>

#include "salticidae/stream.h"
#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/util.h"
#include "hotstuff/client.h"
#include "hotstuff/hotstuff.h"

using salticidae::MsgNetwork;
using salticidae::ClientNetwork;
using salticidae::ElapsedTime;
using salticidae::Config;
using salticidae::_1;
using salticidae::_2;
using salticidae::static_pointer_cast;
using salticidae::trim_all;
using salticidae::split;

using hotstuff::Event;
using hotstuff::EventContext;
using hotstuff::NetAddr;
using hotstuff::HotStuffError;
using hotstuff::CommandDummy;
using hotstuff::Finality;
using hotstuff::command_t;
using hotstuff::uint256_t;
using hotstuff::bytearray_t;
using hotstuff::DataStream;
using hotstuff::ReplicaID;
using hotstuff::MsgClient;
using hotstuff::get_hash;
using hotstuff::promise_t;

using HotStuff = hotstuff::HotStuffSecp256k1;

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN
#define LOG_ERROR HOTSTUFF_LOG_ERROR

class HotStuffApp: public HotStuff {
    double stat_period;
    EventContext eb;
    /** Network messaging between a replica and its client. */
    ClientNetwork<MsgClient> cn;
    /** Timer object to schedule a periodic printing of system statistics */
    Event ev_stat_timer;
    /** The listen address for client RPC */
    NetAddr clisten_addr;
    /** Maximum number of parents. */
    int32_t parent_limit;

    using conn_client_t = MsgNetwork<MsgClient>::conn_t;

    void client_request_cmd_handler(const MsgClient &, conn_client_t);
    void print_stat_cb(evutil_socket_t, short);

    command_t parse_cmd(DataStream &s) override {
        auto cmd = new CommandDummy();
        s >> *cmd;
        return cmd;
    }

    void state_machine_execute(const Finality &fin) override {
        LOG_INFO("replicated %s", std::string(fin).c_str());
    }

    public:
    HotStuffApp(uint32_t blk_size,
                int32_t parent_limit,
                double stat_period,
                ReplicaID idx,
                const bytearray_t &raw_privkey,
                NetAddr plisten_addr,
                NetAddr clisten_addr,
                const EventContext &eb);

    void start();
};

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = trim_all(split(s, ";"));
    if (ret.size() != 2)
        throw std::invalid_argument("invalid cport format");
    return std::make_pair(ret[0], ret[1]);
}

void signal_handler(int) {
    throw HotStuffError("got terminal signal");
}

salticidae::BoxObj<HotStuffApp> papp = nullptr;

int main(int argc, char **argv) {
    Config config("hotstuff.conf");

    ElapsedTime elapsed;
    elapsed.start();

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    auto opt_blk_size = Config::OptValInt::create(1);
    auto opt_parent_limit = Config::OptValInt::create(-1);
    auto opt_stat_period = Config::OptValDouble::create(10);
    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_idx = Config::OptValInt::create(0);
    auto opt_client_port = Config::OptValInt::create(-1);
    auto opt_privkey = Config::OptValStr::create();
    auto opt_help = Config::OptValFlag::create(false);

    config.add_opt("block-size", opt_blk_size, Config::SET_VAL);
    config.add_opt("parent-limit", opt_parent_limit, Config::SET_VAL);
    config.add_opt("stat-period", opt_stat_period, Config::SET_VAL);
    config.add_opt("replica", opt_replicas, Config::APPEND);
    config.add_opt("idx", opt_idx, Config::SET_VAL);
    config.add_opt("cport", opt_client_port, Config::SET_VAL);
    config.add_opt("privkey", opt_privkey, Config::SET_VAL);
    config.add_opt("help", opt_help, Config::SWITCH_ON, 'h', "show this help info");

    EventContext eb;
#ifndef HOTSTUFF_ENABLE_LOG_DEBUG
    try {
#endif
        config.parse(argc, argv);
        if (opt_help->get())
        {
            config.print_help();
            exit(0);
        }
        auto idx = opt_idx->get();
        auto client_port = opt_client_port->get();
        std::vector<std::pair<std::string, std::string>> replicas;
        for (const auto &s: opt_replicas->get())
        {
            auto res = trim_all(split(s, ","));
            if (res.size() != 2)
                throw HotStuffError("invalid replica info");
            replicas.push_back(std::make_pair(res[0], res[1]));
        }

        if (!(0 <= idx && (size_t)idx < replicas.size()))
            throw HotStuffError("replica idx out of range");
        std::string binding_addr = replicas[idx].first;
        if (client_port == -1)
        {
            auto p = split_ip_port_cport(binding_addr);
            size_t idx;
            try {
                client_port = stoi(p.second, &idx);
            } catch (std::invalid_argument &) {
                throw HotStuffError("client port not specified");
            }
        }

        NetAddr plisten_addr{split_ip_port_cport(binding_addr).first};

        papp = new HotStuffApp(opt_blk_size->get(),
                            opt_parent_limit->get(),
                            opt_stat_period->get(),
                            idx,
                            hotstuff::from_hex(opt_privkey->get()),
                            plisten_addr,
                            NetAddr("0.0.0.0", client_port),
                            eb);
        for (size_t i = 0; i < replicas.size(); i++)
        {
            auto p = split_ip_port_cport(replicas[i].first);
            papp->add_replica(i, NetAddr(p.first),
                                hotstuff::from_hex(replicas[i].second));
        }
        papp->start();
#ifndef HOTSTUFF_ENABLE_LOG_DEBUG
    } catch (std::exception &e) {
        HOTSTUFF_LOG_INFO("exception: %s", e.what());
        elapsed.stop(true);
    }
#endif
    return 0;
}

HotStuffApp::HotStuffApp(uint32_t blk_size,
                        int32_t parent_limit,
                        double stat_period,
                        ReplicaID idx,
                        const bytearray_t &raw_privkey,
                        NetAddr plisten_addr,
                        NetAddr clisten_addr,
                        const EventContext &eb):
    HotStuff(blk_size, idx, raw_privkey,
            plisten_addr,
            new hotstuff::PaceMakerDummyFixed(1, parent_limit), eb),
    stat_period(stat_period),
    eb(eb),
    cn(eb),
    clisten_addr(clisten_addr),
    parent_limit(parent_limit) {
    /* register the handlers for msg from clients */
    cn.reg_handler(hotstuff::REQ_CMD, std::bind(&HotStuffApp::client_request_cmd_handler, this, _1, _2));
    cn.listen(clisten_addr);
}

void HotStuffApp::client_request_cmd_handler(const MsgClient &msg, conn_client_t conn_) {
    auto conn = static_pointer_cast<ClientNetwork<MsgClient>::Conn>(conn_);
    const NetAddr addr = conn->get_addr();
    command_t cmd;
    std::vector<promise_t> pms;
    msg.parse_reqcmd(cmd, this);

    bool flag = true;
#ifndef HOTSTUFF_DISABLE_TX_VERIFY
    flag &= cmd->verify();
#endif
    const uint256_t cmd_hash = cmd->get_hash();
    if (!flag)
    {
        LOG_WARN("invalid client cmd");
        MsgClient resp;
        resp.gen_respcmd(Finality(get_id(), -1, 0, 0, cmd_hash, uint256_t()));
        cn.send_msg(resp, addr);
    }
    else
    {
        LOG_DEBUG("processing %s", std::string(*cmd).c_str());
        exec_command(cmd).then([this, addr](Finality fin) {
            MsgClient resp;
            resp.gen_respcmd(fin);
            cn.send_msg(resp, addr);
        });
    }
}

void HotStuffApp::start() {
    ev_stat_timer = Event(eb, -1, 0,
            std::bind(&HotStuffApp::print_stat_cb, this, _1, _2));
    ev_stat_timer.add_with_timeout(stat_period);
    LOG_INFO("** starting the system with parameters **");
    LOG_INFO("blk_size = %lu", blk_size);
    LOG_INFO("parent_limit = %d", parent_limit);
    LOG_INFO("conns = %lu", HotStuff::size());
    LOG_INFO("** starting the event loop...");
#ifdef HOTSTUFF_DISABLE_TX_VERIFY
    LOG_INFO("!! verification disabled !!");
#else
    LOG_INFO("** verification enabled **");
#endif
    HotStuff::start();
    /* enter the event main loop */
    eb.dispatch();
}


void HotStuffApp::print_stat_cb(evutil_socket_t, short) {
    HotStuff::print_stat();
    HotStuffCore::prune(100);
    ev_stat_timer.add_with_timeout(stat_period);
}
