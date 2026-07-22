#include <csignal>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "orin_thermal_ioctl.h"
#include "RingBuffer.h"

enum class ThermalState {NORMAL, WARM, HOT, CRITICAL};

std::atomic<bool>* g_running_ptr = nullptr;

ThermalState getState(float temp, std::atomic<ThermalState>& current){
    
    ThermalState val = current.load();

    switch(val){
        case ThermalState::NORMAL:
            if(temp >= 45){
                val = ThermalState::WARM;
            }
            break;
        case ThermalState::WARM:
            if(temp >= 50){
                val = ThermalState::HOT;
            }
            else if(temp < 43){
                val = ThermalState::NORMAL;
            }
            break;
        case ThermalState::HOT:
            if(temp >= 55){
                val = ThermalState::CRITICAL;
            }
            else if(temp < 48){
                val = ThermalState::WARM;
            }
            break;
        case ThermalState::CRITICAL:
            if(temp < 53){
                val = ThermalState::HOT;
            }
            break;
    }
    return val;
}

//thread gets frames from camera
void producer_thread(RingBuffer& buffer, std::atomic<ThermalState>& current_state, 
    std::atomic<bool>& running, std::atomic<int>& droppedFrames){

    int current_id = 0;

    while(running.load()){
        DummyFrame frame;
        frame.frame_id = current_id;
        frame.data[current_id % 1024] = 0;
        frame.time_stamp = std::chrono::steady_clock::now();

        if (!buffer.push(frame)) {
            droppedFrames++;
        } 
        
        current_id++;
        ThermalState state = current_state.load();
        if(state == ThermalState::WARM){
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            std::cout << "WARM|" << "Dropped: " << droppedFrames << std::endl;
        }
        else if(state == ThermalState::HOT){
            std::this_thread::sleep_for(std::chrono::milliseconds(67));
            std::cout << "HOT|" << "Dropped: " << droppedFrames << std::endl;
        }
        else if(state == ThermalState::CRITICAL){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "CRITICAL|" << "Dropped: " << droppedFrames << std::endl;
        }
        else{
             std::this_thread::sleep_for(std::chrono::milliseconds(33));
             std::cout << "NORMAL|" << "Dropped: " << droppedFrames << std::endl;
        }
    }
}
//gets frames from buffer and runs tensorRT
void consumer_thread(RingBuffer& buffer, std::atomic<ThermalState>& current_state, std::atomic<bool>& running,std::atomic<float>& temperature){
    while(running.load()){
        DummyFrame frame = buffer.pop(running);

        auto now = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - frame.time_stamp).count();

        std::cout << "[Consumer] Processed Frame " << frame.frame_id 
                  << " | Queue Latency: " << latency << " ms | Temperature: " <<  temperature.load() << "\n";

        ThermalState state = current_state.load();
        
        if(state == ThermalState::WARM){
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        else if(state == ThermalState::HOT){
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        else if(state == ThermalState::CRITICAL){
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
        }
        else{
             std::this_thread::sleep_for(std::chrono::milliseconds(45));
        }
    }
}

//checks the cpu temp 
void governor_thread(std::atomic<float>& temperature, std::atomic<ThermalState>& current_state, std::atomic<bool>& running){
    int fd = open("/dev/JETSON", O_RDWR);
    if (fd < 0) {
        perror("governor: failed to open /dev/JETSON");
        return;
    }

    while(running.load()){
        struct thermal_telemetry gpu_data;
        //if success
        if (ioctl(fd, JETSON_THERMAL_GPU_READ, &gpu_data) == 0) {
            temperature.store(static_cast<float>(gpu_data.temperature));
        }
        else{
            perror("governor: ioctl failed");
        }

        ThermalState next_state = getState(temperature.load(),current_state);
        current_state.store(next_state);

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }
    close(fd);
}

//for ctrl C shutdown
void signalHandler(int) {
    if (g_running_ptr) g_running_ptr->store(false);
}

int main(){
    RingBuffer ring_buffer(10);
    std::atomic<float> temperature = 70.0f;
    std::atomic<ThermalState> currentState = ThermalState::NORMAL; 
    std::atomic<bool> running = true;
    std::atomic<int> droppedFrames{0};

    //stops when ctrl C
    g_running_ptr = &running;
    std::signal(SIGINT, signalHandler);

    std::thread producer(producer_thread, std::ref(ring_buffer), std::ref(currentState), std::ref(running), std::ref(droppedFrames));
    std::thread consumer(consumer_thread, std::ref(ring_buffer), std::ref(currentState), std::ref(running), std::ref(temperature));
    std::thread governor(governor_thread, std::ref(temperature), std::ref(currentState), std::ref(running));

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ring_buffer.notifyShutdown(); //after running == false changes flag so consumer/pop doesnt hang

    producer.join();
    consumer.join();
    governor.join();

    return 0;
}