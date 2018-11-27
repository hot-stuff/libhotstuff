/**
 * Copyright 2018 VMware
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HOTSTUFF_UTIL_H
#define _HOTSTUFF_UTIL_H

#include "hotstuff/config.h"
#include "salticidae/util.h"

namespace hotstuff {

class Logger: public salticidae::Logger {
    public:
    using salticidae::Logger::Logger;

    void proto(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        write("proto", is_tty() ? salticidae::TTY_COLOR_MAGENTA : nullptr, fmt, ap);
        va_end(ap);
    }
};

extern Logger logger;

#ifdef HOTSTUFF_PROTO_LOG
#define HOTSTUFF_ENABLE_LOG_PROTO
#endif

#ifdef HOTSTUFF_DEBUG_LOG
#define HOTSTUFF_NORMAL_LOG
#define HOTSTUFF_ENABLE_LOG_DEBUG
#define HOTSTUFF_ENABLE_LOG_PROTO
#endif

#ifdef HOTSTUFF_NORMAL_LOG
#define HOTSTUFF_ENABLE_LOG_INFO
#define HOTSTUFF_ENABLE_LOG_WARN
#endif

#ifdef HOTSTUFF_ENABLE_LOG_INFO
#define HOTSTUFF_LOG_INFO(...) hotstuff::logger.info(__VA_ARGS__)
#else
#define HOTSTUFF_LOG_INFO(...) ((void)0)
#endif

#ifdef HOTSTUFF_ENABLE_LOG_DEBUG
#define HOTSTUFF_LOG_DEBUG(...) hotstuff::logger.debug(__VA_ARGS__)
#else
#define HOTSTUFF_LOG_DEBUG(...) ((void)0)
#endif

#ifdef HOTSTUFF_ENABLE_LOG_WARN
#define HOTSTUFF_LOG_WARN(...) hotstuff::logger.warning(__VA_ARGS__)
#else
#define HOTSTUFF_LOG_WARN(...) ((void)0)
#endif

#ifdef HOTSTUFF_ENABLE_LOG_PROTO
#define HOTSTUFF_LOG_PROTO(...) hotstuff::logger.proto(__VA_ARGS__)
#else
#define HOTSTUFF_LOG_PROTO(...) ((void)0)
#endif

#define HOTSTUFF_LOG_ERROR(...) hotstuff::logger.error(__VA_ARGS__)

#ifdef HOTSTUFF_BLK_PROFILE
class BlockProfiler {
    enum BlockState {
        BLK_SEEN,
        BLK_FETCH,
        BLK_CC
    };

    struct BlockProfile {
        bool is_local;          /* is the block proposed by the replica itself? */
        BlockState state;
        double hash_seen_time;  /* the first time to see block hash */
        double full_fetch_time; /* the first time to get full block content */
        double cc_time;         /* the time when it receives cc */
        double commit_time;     /* the time when it commits */
    };

    std::unordered_map<const uint256, BlockProfile> blocks;
    ElapsedTime timer;

    public:
    BlockProfiler() { timer.start(); }

    auto rec_blk(const uint256 &blk_hash, bool is_local) {
        auto it = blocks.find(blk_hash);
        assert(it == blocks.end());
        timer.stop(false);
        return blocks.insert(std::make_pair(blk_hash,
            BlockProfile{is_local, BLK_SEEN, timer.elapsed_sec, 0, 0, 0})).first;
    }

    void get_blk(const uint256 &blk_hash) {
        auto it = blocks.find(blk_hash);
        if (it == blocks.end())
            it = rec_blk(blk_hash, false);
        BlockProfile &blkp = it->second;
        assert(blkp.state == BLK_SEEN);
        timer.stop(false);
        blkp.full_fetch_time = timer.elapsed_sec;
        blkp.state = BLK_FETCH;
    }

    void have_cc(const uint256 &blk_hash) {
        auto it = blocks.find(blk_hash);
        assert(it != blocks.end());
        BlockProfile &blkp = it->second;
        assert(blkp.state == BLK_FETCH);
        timer.stop(false);
        blkp.polling_start_time = timer.elapsed_sec;
        blkp.state = BLK_CC;
    }

    const char *decide_blk(const uint256 &blk_hash) {
        static char buff[1024];
        auto it = blocks.find(blk_hash);
        assert(it != blocks.end());
        BlockProfile &blkp = it->second;
        assert(blkp.state == BLK_CC);
        timer.stop(false);
        blkp.commit_time = timer.elapsed_sec;
        snprintf(buff, sizeof buff, "(%d,%.4f,%.4f,%.4f,%.4f,%.4f)",
                blkp.is_local,
                blkp.hash_seen_time, blkp.full_fetch_time,
                blkp.polling_start_time, blkp.polling_end_time,
                blkp.commit_time);
        blocks.erase(it);
        return buff;
    }
};

#endif

}

#endif
