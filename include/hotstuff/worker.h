#ifndef _HOTSTUFF_WORKER_H
#define _HOTSTUFF_WORKER_H

#include <thread>
#include <unordered_map>
#include <unistd.h>
#include "concurrentqueue/blockingconcurrentqueue.h"

namespace hotstuff {

class VeriTask {
    friend class VeriPool;
    bool result;
    public:
    virtual bool verify() = 0;
    virtual ~VeriTask() = default;
};

using veritask_ut = BoxObj<VeriTask>;

class VeriPool {
    using queue_t = moodycamel::BlockingConcurrentQueue<VeriTask *>;
    int fin_fd[2];
    FdEvent fin_ev;
    queue_t in_queue;
    queue_t out_queue;
    std::thread notifier;
    std::vector<std::thread> workers;
    std::unordered_map<VeriTask *, std::pair<veritask_ut, promise_t>> pms;
    public:
    VeriPool(EventContext ec, size_t nworker) {
        pipe(fin_fd);
        fin_ev = FdEvent(ec, fin_fd[0], [&](int fd, short) {
            VeriTask *task;
            bool result;
            read(fd, &task, sizeof(VeriTask *));
            read(fd, &result, sizeof(bool));
            auto it = pms.find(task);
            it->second.second.resolve(result);
            pms.erase(it);
            fin_ev.add(FdEvent::READ);
        });
        fin_ev.add(FdEvent::READ);
        // finish notifier thread
        notifier = std::thread([this]() {
            while (true)
            {
                VeriTask *task;
                out_queue.wait_dequeue(task);
                write(fin_fd[1], &task, sizeof(VeriTask *));
                write(fin_fd[1], &(task->result), sizeof(bool));
            }
        });
        for (size_t i = 0; i < nworker; i++)
        {
            workers.push_back(std::thread([this]() {
                while (true)
                {
                    VeriTask *task;
                    in_queue.wait_dequeue(task);
                    //fprintf(stderr, "%lu working on %u\n", std::this_thread::get_id(), (uintptr_t)task);
                    task->result = task->verify();
                    out_queue.enqueue(task);
                }
            }));
        }
    }

    ~VeriPool() {
        notifier.detach();
        for (auto &w: workers) w.detach();
        close(fin_fd[0]);
        close(fin_fd[1]);
    }

    promise_t verify(veritask_ut &&task) {
        auto ptr = task.get();
        auto ret = pms.insert(std::make_pair(ptr,
                std::make_pair(std::move(task), promise_t([](promise_t &){}))));
        assert(ret.second);
        in_queue.enqueue(ptr);
        return ret.first->second.second;
    }

    int get_fd() {
        return fin_fd[0];
    }
};

}

#endif
