#include <iostream>
#include <array>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <random>
#include <thread>
#include <atomic>

using time_point = std::chrono::steady_clock::time_point;

struct DummyFrame{
    int frame_id;
    std::array<uint8_t, 1024> data;
    time_point time_stamp;
};

class RingBuffer{
    private:
        std::vector<DummyFrame> buffer;
        size_t head;
        size_t tail;
        size_t capacity_max;
        size_t current_count;

        std::mutex mtx;
        std::condition_variable cv_not_full;
        std::condition_variable cv_not_empty;

    public:
        RingBuffer(size_t size) : capacity_max(size), buffer(size),head(0),tail(0){}
        void reset();
        bool full() const { return current_count == capacity_max; };
        bool empty() const { return current_count == 0; };
        size_t size();
        size_t get_capacity();
        bool push(const DummyFrame& frame);
        DummyFrame pop(std::atomic<bool>& running);
        void notifyShutdown();
};