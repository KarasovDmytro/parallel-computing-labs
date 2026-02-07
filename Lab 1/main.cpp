#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>

const int MAX_VAL = 1000;

void generateData(std::vector<int>& data) {
    std::mt19937 eng(777);
    std::uniform_int_distribution<> distribution(0, MAX_VAL);
    for (auto& val : data) {
        val = distribution(eng);
    }
}

void solveSequential(const std::vector<int>& data, double& medianOut, int& modeOut) {

    std::vector<int> frequences(MAX_VAL + 1, 0);

    for (int val : data) {
        frequences[val]++;
    }

    int mode = 0;
    int maxCount = -1;

    for (int i = 0; i <= MAX_VAL; i++) {
        if (frequences[i] > maxCount) {
            maxCount = frequences[i];
            mode = i;
        }
    }

    modeOut = mode;

    long long count = 0;
    size_t n = data.size();

    int m1 = -1;
    int m2 = -1;

    size_t target1 = (n % 2 == 0) ? (n / 2) : (n / 2 + 1);
    size_t target2 = n / 2 + 1;

    for (int i = 0; i <= MAX_VAL; i++) {
        count += frequences[i];

        if (m1 == -1 && count >= target1) {
            m1 = i;
        }

        if (count >= target2) {
            m2 = i;
            break;
        }
    }

    medianOut = (m1 + m2) / 2.0;
}

void freqCountThread(const std::vector<int>& data, size_t start, size_t end, std::vector<int>& localFreq) {
    for (size_t i = start; i < end; i++) {
        localFreq[data[i]]++;
    }
}

void solveParallel(const std::vector<int>& data, int nThreads, double& medianOut, int& modeOut) {

    std::vector<std::thread> threads;
    std::vector<std::vector<int>> localFreqs(nThreads, std::vector<int>(MAX_VAL + 1, 0));

    size_t chainSize = data.size() / nThreads;

    //auto startCreate = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < nThreads; i++) {
        size_t start = i * chainSize;
        size_t end = (i == nThreads - 1) ? data.size() : start + chainSize;
        threads.emplace_back(freqCountThread, std::cref(data), start, end, std::ref(localFreqs[i]));
    }

    //auto endCreate = std::chrono::high_resolution_clock::now();
    //std::chrono::duration<double> createTime = endCreate - startCreate;

    //std::cout << "Creation time (" << nThreads << " threads): " << std::fixed << std::setprecision(6) << createTime.count() << " sec\n";

    for (auto& t : threads) {
        t.join();
    }

    std::vector<int> globalFreq(MAX_VAL + 1, 0);
    for (const auto& freq : localFreqs) {
        for (int i = 0; i <= MAX_VAL; i++) {
            globalFreq[i] += freq[i];
        }
    }

    int mode = 0;
    int maxCount = -1;

    for (int i = 0; i <= MAX_VAL; i++) {
        if (globalFreq[i] > maxCount) {
            maxCount = globalFreq[i];
            mode = i;
        }
    }
    modeOut = mode;

    long long count = 0;
    size_t n = data.size();

    int m1 = -1;
    int m2 = -1;

    size_t target1 = (n % 2 == 0) ? (n / 2) : (n / 2 + 1);
    size_t target2 = n / 2 + 1;

    for (int i = 0; i <= MAX_VAL; i++) {
        count += globalFreq[i];
        if (m1 == -1 && count >= target1) m1 = i;
        if (count >= target2) {
            m2 = i;
            break;
        }
    }

    medianOut = (m1 + m2) / 2.0;
}

int main() {
    int THREADS_TO_TEST = 128;

    std::cout << "=== Verification ===\n";
    std::vector<int> small = { 920, 32, 808, 353, 754, 40, 139, 808, 87, 341 };

    // Sorted:
    // 32, 40, 87, 139, 341, 353, 754, 808, 808, 920

    double m; int mo;
    solveParallel(small, 4, m, mo);

    std::cout << "Data: ";
    for (int x : small) {
        std::cout << x << " ";
    }

    std::cout << "\nExpected Mode: 808, Median: 347\n";
    std::cout << "Got Mode: " << mo << ", Median: " << m << "\n";
    std::cout << "====================\n\n";

    std::vector<size_t> sizes = { 1000000, 10000000, 50000000, 100000000 };

    // Anti cold start
    std::vector<int> warmup(100000);
    generateData(warmup);
    double wm; int wmo;
    solveSequential(warmup, wm, wmo);
    solveParallel(warmup, 4, wm, wmo);

    std::cout << std::left << std::setw(15) << "N"
        << std::setw(15) << "Seq(sec)"
        << std::setw(15) << "Par(sec)"
        << std::setw(15) << "Speedup" << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (size_t N : sizes) {
        std::vector<int> data(N);
        generateData(data);

        double medS, medP;
        int modS, modP;

        auto startS = std::chrono::high_resolution_clock::now();
        solveSequential(data, medS, modS);
        auto endS = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> timeS = endS - startS;

        auto startP = std::chrono::high_resolution_clock::now();
        solveParallel(data, THREADS_TO_TEST, medP, modP);
        auto endP = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> timeP = endP - startP;

        double speedup = timeS.count() / timeP.count();

        if (medS == 123 && modS == 123) {
            std::cout << "123\n";
        }

        if (medP == 321 && modP == 321) {
            std::cout << "321\n";
        }

        std::cout << std::left << std::setw(15) << N
            << std::setw(15) << std::fixed << std::setprecision(6) << timeS.count()
            << std::setw(15) << std::fixed << std::setprecision(6) << timeP.count()
            << "x" << std::fixed << std::setprecision(2) << speedup << "\n";
    }

    return 0;
}