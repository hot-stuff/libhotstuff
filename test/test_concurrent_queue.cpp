#include "salticidae/event.h"
#include "concurrentqueue/blockingconcurrentqueue.h"
#include <thread>
#include <unistd.h>

class VeriPool {
    int fin_fd[2];
    moodycamel::BlockingConcurrentQueue<int> in_queue;
    moodycamel::BlockingConcurrentQueue<int> out_queue;
    std::thread notifier;
    std::vector<std::thread> workers;
    public:
    VeriPool(size_t nworker) {
        pipe(fin_fd);
        // finish notifier thread
        notifier = std::thread([this]() {
            while (true)
            {
                int item;
                out_queue.wait_dequeue(item);
                write(fin_fd[1], &item, sizeof(item));
            }
        });
        for (size_t i = 0; i < nworker; i++)
        {
            workers.push_back(std::thread([this]() {
                while (true)
                {
                    int item;
                    in_queue.wait_dequeue(item);
                    fprintf(stderr, "%lu working on %d\n", std::this_thread::get_id(), item);
                    out_queue.enqueue(item * 1000);
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

    void submit(int item) {
        in_queue.enqueue(item);
    }

    int get_fd() {
        return fin_fd[0];
    }
};

int main() {
    VeriPool p(2);
    salticidae::EventContext ec;
    salticidae::Event ev;
    ev = salticidae::Event(ec, p.get_fd(), EV_READ, [&ev](int fd, short) {
        int item;
        read(fd, &item, sizeof(item));
        printf("finished %d\n", item);
        ev.add();
    });
    for (int i = 0; i < 10000; i++)
        p.submit(i);
    ev.add();
    ec.dispatch();
}
