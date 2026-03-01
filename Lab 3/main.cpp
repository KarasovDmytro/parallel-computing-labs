#include <iostream>
#include <string>
#include <thread>
#include <functional>
#include <random>
#include <chrono>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>

using namespace std;

atomic<int> taskIdCounter{ 1 };
mutex logMutex;

void printLog(const string& msg) {
    lock_guard<mutex> lock(logMutex);
    cout << msg << "\n";
}

struct Task {
    int id;
    int durationSec;
    function<void()> todo;
};

class ThreadPool {
private:
    queue<Task> tasksQueue;
    vector<thread> workers;
    thread timerThread;
    mutable shared_mutex rw_mutex;

    condition_variable_any wait_for_tasks;
    condition_variable_any wait_for_execution;

    bool initialized = false;
    bool terminated = false;
    bool isExecutingPhase = false;
    bool isPaused = false;

    int activeWorkers = 0;
    int declinedTasksAmount = 0;

    atomic<long long> total_wait_time_ms{ 0 };
    atomic<long long> total_exec_time_ms{ 0 };
    atomic<int> completed_tasks{ 0 };
    atomic<int> max_queue_length_sum{ 0 };
    atomic<int> execution_phases_count{ 0 };

    void routine() {
        while (true) {
            Task task;
            {
                unique_lock<shared_mutex> lock(rw_mutex);

                auto wait_start = chrono::steady_clock::now();
                wait_for_tasks.wait(lock, [this]() {
                    return (isExecutingPhase && !tasksQueue.empty() && !isPaused) || terminated;
                    });
                auto wait_end = chrono::steady_clock::now();
                total_wait_time_ms += chrono::duration_cast<chrono::milliseconds>(wait_end - wait_start).count();

                if (terminated && tasksQueue.empty()) {
                    return;
                }

                task = move(tasksQueue.front());
                tasksQueue.pop();
                activeWorkers++;
            }

            ostringstream oss;
            oss << "  [~] PROCESSING: Thread " << this_thread::get_id() << " is working on Task #" << task.id;
            printLog(oss.str());

            auto exec_start = chrono::steady_clock::now();
            task.todo();
            auto exec_end = chrono::steady_clock::now();
            total_exec_time_ms += chrono::duration_cast<chrono::milliseconds>(exec_end - exec_start).count();
            completed_tasks++;

            {
                unique_lock<shared_mutex> lock(rw_mutex);
                activeWorkers--;

                if (tasksQueue.empty() && activeWorkers == 0) {
                    wait_for_execution.notify_one();
                }
            }
        }
    }

    void timerRoutine() {
        while (true) {
            {
                unique_lock<shared_mutex> lock(rw_mutex);

                int secondsPassed = 0;

                while (secondsPassed < 45) {
                    wait_for_execution.wait_for(lock, chrono::seconds(1), [this]() {
                        return terminated;
                        });

                    if (terminated) {
                        return;
                    }

                    if (!isPaused) {
                        secondsPassed++;
                    }
                }

                isExecutingPhase = true;
                max_queue_length_sum += tasksQueue.size();
                execution_phases_count++;

                printLog("\n==========================================================");
                printLog("[*] TIMER: Execution phase STARTED! Tasks in queue: " + to_string(tasksQueue.size()));
                printLog("==========================================================");
            }

            wait_for_tasks.notify_all();

            {
                unique_lock<shared_mutex> lock(rw_mutex);

                wait_for_execution.wait(lock, [this]() {
                    return (tasksQueue.empty() && activeWorkers == 0) || terminated;
                    });

                if (terminated) {
                    return;
                }

                isExecutingPhase = false;
                printLog("\n==========================================================");
                printLog("[*] TIMER: All tasks DONE. Collecting phase STARTED (45s)...");
                printLog("==========================================================\n");
            }
        }
    }

public:
    ThreadPool(int nWorkers) {
        unique_lock<shared_mutex> lock(rw_mutex);

        if (initialized || terminated) {
            return;
        }

        for (int i = 0; i < nWorkers; i++) {
            workers.emplace_back(&ThreadPool::routine, this);
        }

        timerThread = thread(&ThreadPool::timerRoutine, this);
        initialized = !workers.empty();
    }

    void pause() {
        unique_lock<shared_mutex> lock(rw_mutex);
        isPaused = true;
        printLog("\n[!] POOL PAUSED. Workers will not take new tasks.");
    }

    void resume() {
        {
            unique_lock<shared_mutex> lock(rw_mutex);
            isPaused = false;
            printLog("\n[!] POOL RESUMED. Workers can work again.");
        }
        wait_for_tasks.notify_all();
    }

    bool working() {
        shared_lock<shared_mutex> lock(rw_mutex);
        return initialized && !terminated && !isPaused;
    }

    void addTask(Task task) {
        unique_lock<shared_mutex> lock(rw_mutex);

        if (terminated || !initialized) {
            return;
        }

        if (isPaused) {
            declinedTasksAmount++;
            printLog("  [-] DECLINED: Task #" + to_string(task.id) + " rejected (Pool is PAUSED)");
            return;
        }

        if (isExecutingPhase) {
            declinedTasksAmount++;
            printLog("  [-] DECLINED: Task #" + to_string(task.id) + " rejected (Execution phase is active)");
            return;
        }

        tasksQueue.push(move(task));
        printLog("  [+] ACCEPTED: Task #" + to_string(task.id) + " queued (Duration: " + to_string(task.durationSec) + "s)");
    }

    int getDiscardedCount() {
        shared_lock<shared_mutex> lock(rw_mutex);
        return declinedTasksAmount;
    }

    void terminate(bool immediate) {
        {
            unique_lock<shared_mutex> lock(rw_mutex);
            if (terminated) {
                return;
            }

            terminated = true;

            if (immediate) {
                printLog("\n[X] IMMEDIATE SHUTDOWN: Dropping all waiting tasks!");
                queue<Task> emptyQueue;
                swap(tasksQueue, emptyQueue);
            }
            else {
                printLog("\n[V] GRACEFUL SHUTDOWN: Waiting for queue to finish...");
            }
        }

        wait_for_tasks.notify_all();
        wait_for_execution.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        if (timerThread.joinable()) {
            timerThread.join();
        }
    }

    void printAnalysis() {
        printLog("\n--- Pool work analysis ---");
        printLog("Number of working threads: " + to_string(workers.size()));

        if (completed_tasks > 0) {
            printLog("Completed tasks: " + to_string(completed_tasks));
            printLog("Mean task completion time: " + to_string(total_exec_time_ms / completed_tasks / 1000.0) + " sec");
        }

        if (execution_phases_count > 0) {
            printLog("Medium queue size: " + to_string(max_queue_length_sum / execution_phases_count));
        }

        printLog("Average total waiting time for 1 stream: " + to_string(total_wait_time_ms / workers.size() / 1000.0) + " sec");
        printLog("Declined tasks: " + to_string(declinedTasksAmount));
    }

    ~ThreadPool() {
        terminate(false);
    }
};

void producer(ThreadPool& pool, int producerId) {
    thread_local mt19937 rng(random_device{}());
    uniform_int_distribution<int> durationDist(6, 12);
    uniform_int_distribution<int> sleepDist(4, 10);

    for (int i = 0; i < 10; i++) {
        this_thread::sleep_for(chrono::seconds(sleepDist(rng)));

        Task task;
        task.id = taskIdCounter++;
        task.durationSec = durationDist(rng);

        task.todo = [id = task.id, duration = task.durationSec, producerId]() {
            this_thread::sleep_for(chrono::seconds(duration));
            printLog("    [v] DONE: Task #" + to_string(id) + " (Producer " + to_string(producerId) + ") finished in " + to_string(duration) + "s");
            };

        pool.addTask(move(task));
    }
}

int main() {
    printLog("--- POOL WORK TEST ---\n");

    ThreadPool pool(4);
    vector<thread> producers;

    for (int i = 1; i <= 3; i++) {
        producers.emplace_back(producer, ref(pool), i);
    }

    for (auto& p : producers) {
        p.join();
    }

    printLog("\n[!] MAIN THREAD: Testing PAUSE...");
    pool.pause();
    this_thread::sleep_for(chrono::seconds(20));
    pool.resume();

    printLog("\n[!] MAIN THREAD: Waiting 30s for some processing...");
    this_thread::sleep_for(chrono::seconds(30));

    printLog("\n[!] MAIN THREAD: Initiating IMMEDIATE shutdown!");
    pool.terminate(true);

    printLog("--- POOL FINISHED ITS WORK ---");
    printLog("Total declined tasks: " + to_string(pool.getDiscardedCount()));

    return 0;
}