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

                int timePassed = 0;
                while (timePassed < 45) {
                    wait_for_execution.wait_for(lock, chrono::seconds(1), [this]() {
                        return terminated;
                        });

                    if (terminated) {
                        return;
                    }

                    if (!isPaused) {
                        timePassed++;
                    }
                }

                isExecutingPhase = true;

                max_queue_length_sum += tasksQueue.size();
                execution_phases_count++;
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
    }

    void resume() {
        {
            unique_lock<shared_mutex> lock(rw_mutex);
            isPaused = false;
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

        if (isPaused || isExecutingPhase) {
            declinedTasksAmount++;
            return;
        }

        tasksQueue.push(move(task));
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
                queue<Task> emptyQueue;
                swap(tasksQueue, emptyQueue);
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
        shared_lock<shared_mutex> lock(rw_mutex);

        printLog("\n--- POOL WORK STATISTIC ---");
        printLog("Amount of working threads: " + to_string(workers.size()));

        if (completed_tasks > 0) {
            printLog("Tasks done: " + to_string(completed_tasks));
            printLog("Mean task completion time: " + to_string(total_exec_time_ms / completed_tasks / 1000.0) + " sec");
        }

        if (execution_phases_count > 0) {
            printLog("Execution stages passed: " + to_string(execution_phases_count));
            printLog("Mean queue size: " + to_string(max_queue_length_sum / execution_phases_count));
        }

        printLog("Mean waiting time per 1 thread: " + to_string(total_wait_time_ms / workers.size() / 1000.0) + " sec");
        printLog("Tasks declined: " + to_string(declinedTasksAmount));
        printLog("------------------------------\n");
    }

    ~ThreadPool() {
        terminate(false);
    }
};

void producer(ThreadPool& pool, int producerId, atomic<bool>& isRunning) {
    thread_local mt19937 rng(random_device{}());
    uniform_int_distribution<int> durationDist(6, 12);
    uniform_int_distribution<int> sleepDist(4, 10);

    while (isRunning) {
        this_thread::sleep_for(chrono::seconds(sleepDist(rng)));

        if (!isRunning) {
            break;
        }

        Task task;
        task.id = taskIdCounter++;
        task.durationSec = durationDist(rng);

        task.todo = [id = task.id, duration = task.durationSec, producerId]() {
            this_thread::sleep_for(chrono::seconds(duration));
            };

        pool.addTask(move(task));
    }
}

void runTest(int testDurationSeconds) {
    taskIdCounter = 1;

    ThreadPool pool(4);
    vector<thread> producers;

    atomic<bool> isRunning{ true };

    for (int i = 1; i <= 3; i++) {
        producers.emplace_back(producer, ref(pool), i, ref(isRunning));
    }

    this_thread::sleep_for(chrono::seconds(testDurationSeconds));

    isRunning = false;
    pool.terminate(false);

    for (auto& p : producers) {
        if (p.joinable()) {
            p.join();
        }
    }

    pool.printAnalysis();
}

int main() {
    printLog("Starting the test for 60 sec...");
    runTest(60);

    printLog("Starting the test for 150 sec...");
    runTest(150);

    printLog("Starting the test for 300 sec...");
    runTest(300);

    return 0;
}