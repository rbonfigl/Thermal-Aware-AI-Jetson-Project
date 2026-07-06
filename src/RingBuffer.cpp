#include "RingBuffer.h"

//reset the pointers and count
void RingBuffer::reset(){
    std::lock_guard<std::mutex> lock(mtx);

    head = 0;
    tail = 0;
    current_count = 0;
}

size_t RingBuffer::size(){
    std::lock_guard<std::mutex> lock(mtx);
    return current_count;
}

size_t RingBuffer::get_capacity(){
    return capacity_max;
}

bool RingBuffer::push(const DummyFrame& frame){
    std::lock_guard<std::mutex> lock(mtx);

    if(full()){
        return false;
    }

    buffer[head] = frame;
    current_count++;
    head = (head + 1) % capacity_max;
    //notify consumer
    cv_not_empty.notify_one();
    return true;
}

DummyFrame RingBuffer::pop(std::atomic<bool>& running){
    std::unique_lock<std::mutex> lock(mtx);
    
    //only look if not empty or running flag
    cv_not_empty.wait(lock, [this, &running](){ 
        return !empty() || !running.load(); 
    });

    if (empty()) {
        return DummyFrame{};
    }

    DummyFrame value = buffer[tail];
    current_count--;
    tail = (tail + 1) % capacity_max;
    //notify the producer
    cv_not_full.notify_one();

    return value;
}

void RingBuffer::notifyShutdown(){
    std::lock_guard<std::mutex> lock(mtx);
    cv_not_empty.notify_all();
    cv_not_full.notify_all();
}