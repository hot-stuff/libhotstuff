#ifndef _HOTSTUFF_WORKER_H
#define _HOTSTUFF_WORKER_H

#include <thread>
#include <unordered_map>
#include <unistd.h>
#include "concurrentqueue/blockingconcurrentqueue.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

template<typename R>
class TaskPool {
    public:
    class Task {
        friend TaskPool;
        R result;
        public:
        virtual R work() = 0;
        virtual ~Task() = default;
    };

    private:
    using queue_t = moodycamel::BlockingConcurrentQueue<Task *>;
    int fin_fd[2];
    Event fin_ev;
    queue_t in_queue;
    queue_t out_queue;
    std::thread notifier;
    std::vector<std::thread> workers;
    std::unordered_map<Task *, std::pair<BoxObj<Task>, promise_t>> pms;
    public:
    TaskPool(EventContext ec, size_t nworker) {
        pipe(fin_fd);
        fin_ev = Event(ec, fin_fd[0], EV_READ, [&](int fd, short) {
            Task *task;
            R result;
            read(fd, &task, sizeof(Task *));
            read(fd, &result, sizeof(R));
            auto it = pms.find(task);
            it->second.second.resolve(result);
            pms.erase(it);
            fin_ev.add();
        });
        fin_ev.add();
        // finish notifier thread
        notifier = std::thread([this]() {
            while (true)
            {
                Task *task;
                out_queue.wait_dequeue(task);
                write(fin_fd[1], &task, sizeof(Task *));
                write(fin_fd[1], &(task->result), sizeof(R));
            }
        });
        for (size_t i = 0; i < nworker; i++)
        {
            workers.push_back(std::thread([this]() {
                while (true)
                {
                    Task *task;
                    in_queue.wait_dequeue(task);
                    //fprintf(stderr, "%lu working on %u\n", std::this_thread::get_id(), (uintptr_t)task);
                    task->result = task->work();
                    out_queue.enqueue(task);
                }
            }));
        }
    }

    ~TaskPool() {
        notifier.detach();
        for (auto &w: workers) w.detach();
        close(fin_fd[0]);
        close(fin_fd[1]);
    }

    promise_t submit(BoxObj<Task> &&task) {
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
