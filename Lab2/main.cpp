#include <iostream>
#include <random>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>

#define N 1000000000
#define MAX_VAL 77777
#define THREADS 4

void generateData(std::vector<int>& data) {
    std::mt19937 eng(777);
    std::uniform_int_distribution<> distribution(-MAX_VAL, MAX_VAL);
    for (auto& val : data) {
        val = distribution(eng);
    }
}

void notParallelSolution(const std::vector<int>& numbers, int& counter, int& maxVal) {
    counter = 0;
    maxVal = INT_MIN;

    for (size_t i = 0; i < numbers.size(); i++) {
        if ((numbers[i] % 2) != 0) {
            counter++;
            maxVal = std::max(maxVal, numbers[i]);
        }
    }
}

void mutexThreadWorker(const std::vector<int>& numbers, size_t start, size_t end, int& globalCounter, int& globalMax, std::mutex& globalVarMtx) {
    int localCounter = 0;
    int localMax = INT_MIN;

    for (size_t i = start; i < end; i++) {
        if ((numbers[i] % 2) != 0) {
            localCounter++;
            localMax = std::max(localMax, numbers[i]);
        }
    }

    std::lock_guard<std::mutex> lock(globalVarMtx);
    globalCounter += localCounter;
    globalMax = std::max(globalMax, localMax);
}

void parallelMutexSolution(const std::vector<int>& numbers, int nThreads, int& globalCounter, int& globalMax) {
    globalCounter = 0;
    globalMax = INT_MIN;

    std::mutex globalMtx;
    std::vector<std::thread> threads;
    size_t chainSize = numbers.size() / nThreads;

    for (int i = 0; i < nThreads; i++) {
        size_t start = i * chainSize;
        size_t end = (i == nThreads - 1) ? numbers.size() : start + chainSize;
        threads.emplace_back(mutexThreadWorker, std::cref(numbers), start, end,
            std::ref(globalCounter), std::ref(globalMax), std::ref(globalMtx));
    }
    for (auto& t : threads){ 
        t.join(); 
    }
}

void atomicThreadWorker(const std::vector<int>& numbers, size_t start, size_t end, std::atomic<int>& globalCounter, std::atomic<int>& globalMax) {

    int localCounter = 0;
    int localMax = INT_MIN;

    for (size_t i = start; i < end; i++) {
        if ((numbers[i] % 2) != 0) {
            localCounter++;
            localMax = std::max(localMax, numbers[i]);
        }
    }

    int expectedCount = globalCounter.load();

    while (!globalCounter.compare_exchange_weak(expectedCount, expectedCount + localCounter)) {

    }

    int expectedMax = globalMax.load();

    while (expectedMax < localMax && !globalMax.compare_exchange_weak(expectedMax, localMax)) {

    }
}

void parallelAtomicSolution(const std::vector<int>& numbers, int nThreads, std::atomic<int>& globalCounter, std::atomic<int>& globalMax) {
    globalCounter.store(0);
    globalMax.store(INT_MIN);

    std::vector<std::thread> threads;
    size_t chainSize = numbers.size() / nThreads;

    for (int i = 0; i < nThreads; i++) {
        size_t start = i * chainSize;
        size_t end = (i == nThreads - 1) ? numbers.size() : start + chainSize;
        threads.emplace_back(atomicThreadWorker, std::cref(numbers), start, end,
            std::ref(globalCounter), std::ref(globalMax));
    }
    for (auto& t : threads){ 
        t.join(); 
    }
}

int main() {
    std::vector<int> numbers(N, 0);
    generateData(numbers);

    int counterSeq, maxValSeq;
    int counterMut, maxValMut;
    std::atomic<int> counterAtom, maxValAtom;
    
    notParallelSolution(numbers, counterSeq, maxValSeq);

    std::cout << "--- Sequential algorithm ---\n";
    auto startSeq = std::chrono::high_resolution_clock::now();
    notParallelSolution(numbers, counterSeq, maxValSeq);
    auto endSeq = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> timeSeq = endSeq - startSeq;
    std::cout << "Odd numbers: " << counterSeq << "\nMax odd: " << maxValSeq << "\n";
    std::cout << "Time: " << std::fixed << std::setprecision(6) << timeSeq.count() << " sec\n\n";

    std::cout << "--- Parallel Mutex algorithm ---\n";
    auto startMut = std::chrono::high_resolution_clock::now();
    parallelMutexSolution(numbers, THREADS, counterMut, maxValMut);
    auto endMut = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> timeMut = endMut - startMut;
    std::cout << "Odd numbers: " << counterMut << "\nMax odd: " << maxValMut << "\n";
    std::cout << "Time: " << std::fixed << std::setprecision(6) << timeMut.count() << " sec\n\n";

    std::cout << "--- Parallel Atomic (CAS) algorithm ---\n";
    auto startAtom = std::chrono::high_resolution_clock::now();
    parallelAtomicSolution(numbers, THREADS, counterAtom, maxValAtom);
    auto endAtom = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> timeAtom = endAtom - startAtom;
    std::cout << "Odd numbers: " << counterAtom.load() << "\nMax odd: " << maxValAtom.load() << "\n";
    std::cout << "Time: " << std::fixed << std::setprecision(6) << timeAtom.count() << " sec\n\n";

    std::cout << "--- Speedup Analysis ---\n";
    double speedupMut = timeSeq.count() / timeMut.count();
    double speedupAtom = timeSeq.count() / timeAtom.count();
    double atomVsMut = timeMut.count() / timeAtom.count();

    std::cout << "Mutex vs Sequential:  x" << std::fixed << std::setprecision(2) << speedupMut << "\n";
    std::cout << "Atomic vs Sequential: x" << std::fixed << std::setprecision(2) << speedupAtom << "\n";

    std::cout << "Atomic vs Mutex: ";
    if (atomVsMut > 1.0) {
        std::cout << "atomic is x" << std::fixed << std::setprecision(2) << atomVsMut << " FASTER\n";
    }
    else {
        std::cout << "atomic is x" << std::fixed << std::setprecision(2) << (1.0 / atomVsMut) << " SLOWER\n";
    }

    return 0;
}