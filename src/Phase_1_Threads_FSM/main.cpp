#include <csignal>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include "orin_thermal_ioctl.h"
#include "RingBuffer.h"

enum class ThermalState {NORMAL, WARM, HOT, CRITICAL};

std::atomic<bool>* g_running_ptr = nullptr;

class Logger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING)
            std::cout << "[TensorRT] " << msg << std::endl;
    }
} gLogger;

std::vector<char> readEngineFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open engine file: " << path << std::endl;
        exit(1);
    }
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

ThermalState getState(float temp, std::atomic<ThermalState>& current){
    
    ThermalState val = current.load();

    switch(val){
        case ThermalState::NORMAL:
            if(temp >= 48){
                val = ThermalState::WARM;
            }
            break;
        case ThermalState::WARM:
            if(temp >= 51){
                val = ThermalState::HOT;
            }
            else if(temp < 47){
                val = ThermalState::NORMAL;
            }
            break;
        case ThermalState::HOT:
            if(temp >= 55){
                val = ThermalState::CRITICAL;
            }
            else if(temp < 49){
                val = ThermalState::WARM;
            }
            break;
        case ThermalState::CRITICAL:
            if(temp < 54){
                val = ThermalState::HOT;
            }
            break;
    }
    return val;
}

//thread gets frames from camera
void producer_thread(RingBuffer& buffer, std::atomic<ThermalState>& current_state, 
    std::atomic<bool>& running, std::atomic<int>& droppedFrames, cv::VideoCapture& cap){

    int current_id = 0;
    int empty_frames = 0;

    while(running.load()){
        cv::Mat cv_frame;
        cap >> cv_frame;

        if (cv_frame.empty()) {
            empty_frames++;
            continue;
        }

        DummyFrame frame;
        frame.frame_id = current_id;
        frame.data = cv_frame.clone();
        frame.time_stamp = std::chrono::steady_clock::now();

        if (!buffer.push(frame)) {
            droppedFrames++;
        } 
                
        current_id++;
        ThermalState state = current_state.load();
        if(state == ThermalState::WARM){
            std::cout << "WARM|" << "Dropped: " << droppedFrames << " Empty: " << empty_frames << std::endl;
        }
        else if(state == ThermalState::HOT){
            std::this_thread::sleep_for(std::chrono::milliseconds(67));
            std::cout << "HOT|" << "Dropped: " << droppedFrames << " Empty: " << empty_frames << std::endl;
        }
        else if(state == ThermalState::CRITICAL){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "CRITICAL|" << "Dropped: " << droppedFrames << " Empty: " << empty_frames << std::endl;
        }
        else{
             std::cout << "NORMAL|" << "Dropped: " << droppedFrames << " Empty: " << empty_frames << std::endl;
        }
    }
}
//gets frames from buffer and runs tensorRT
void consumer_thread(RingBuffer& buffer, std::atomic<ThermalState>& current_state, std::atomic<bool>& running,std::atomic<float>& temperature,
    nvinfer1::IExecutionContext* context, void* inputDevice, void* outputDevice, cudaStream_t stream, size_t inputSize, size_t outputSize){
   
    const int numClasses = 80;
    const int numBoxes = 8400;
    const float confThreshold = 0.5f;
    const float nmsThreshold = 0.4f;

    const std::vector<std::string> classNames = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", 
        "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", 
        "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", 
        "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", 
        "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", 
        "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", 
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", 
        "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", 
        "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", 
        "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
    };
   
    while(running.load()){
        DummyFrame frame = buffer.pop(running);

        auto now = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - frame.time_stamp).count();

        if (frame.data.empty()) {
            continue; //bad frame
        }

        //Preprocess
        auto inferStart = std::chrono::steady_clock::now();
        cv::Mat blob = cv::dnn::blobFromImage(frame.data, 1.0/255.0, cv::Size(640, 640), 
                                                cv::Scalar(0,0,0), true, false);

        cudaMemcpyAsync(inputDevice, blob.ptr<float>(), inputSize, cudaMemcpyHostToDevice, stream);
        context->enqueueV3(stream);

        std::vector<float> output(1 * 84 * 8400);
        cudaMemcpyAsync(output.data(), outputDevice, outputSize, cudaMemcpyDeviceToHost, stream);
        cudaStreamSynchronize(stream);

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> classIds;

        for (int i = 0; i < numBoxes; i++) {
            float maxConf = 0.0f;
            int bestClassId = -1;
            
            
            for (int c = 0; c < numClasses; c++) {
                float conf = output[(4 + c) * numBoxes + i];
                if (conf > maxConf) {
                    maxConf = conf;
                    bestClassId = c;
                }
            }
            
            if (maxConf > confThreshold) {
                
                float cx = output[0 * numBoxes + i];
                float cy = output[1 * numBoxes + i];
                float w  = output[2 * numBoxes + i];
                float h  = output[3 * numBoxes + i];

                
                int left = static_cast<int>(cx - (w / 2.0f));
                int top  = static_cast<int>((cy - (h / 2.0f)) * (480.0f / 640.0f));
                int width = static_cast<int>(w);
                int height = static_cast<int>(h * (480.0f / 640.0f));

                boxes.push_back(cv::Rect(left, top, width, height));
                confidences.push_back(maxConf);
                classIds.push_back(bestClassId);
            }
        }

        // Apply NMS
        std::vector<int> indices;
        cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);

        
        for (int idx : indices) {
            cv::Rect box = boxes[idx];
            int classId = classIds[idx];
            
            
            cv::rectangle(frame.data, box, cv::Scalar(0, 255, 0), 2);
            
            
            std::string className = classNames[classId];
            
            
            std::string label = className + ": " + std::to_string(confidences[idx]).substr(0, 4);
            
            
            int baseLine;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            cv::rectangle(frame.data, cv::Point(box.x, box.y - labelSize.height - 5), 
                          cv::Point(box.x + labelSize.width, box.y), 
                          cv::Scalar(0, 255, 0), cv::FILLED);
            
            
            cv::putText(frame.data, label, cv::Point(box.x, box.y - 5), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1.5);
        }
        // Show the image on screen
        cv::imshow("Jetson Live Feed", frame.data);
        
        if (cv::waitKey(1) == 27) {
            running.store(false);
        }

        std::cout << "[Consumer] Processed Frame " << frame.frame_id 
                  << " | Queue Latency: " << latency << " ms | Temperature: " <<  temperature.load() << "\n";

        auto inferEnd = std::chrono::steady_clock::now();
        auto inferMs = std::chrono::duration_cast<std::chrono::milliseconds>(inferEnd - inferStart).count();
        std::cout << "  [Inference took: " << inferMs << " ms]\n";

        ThermalState state = current_state.load();
        
        if(state == ThermalState::HOT){
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
        }
        else if(state == ThermalState::CRITICAL){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

        ThermalState old_state = current_state.load();
        ThermalState next_state = getState(temperature.load(),current_state);
        current_state.store(next_state);

        if (next_state == ThermalState::CRITICAL && old_state != ThermalState::CRITICAL) {
            std::cout << "\n[EMERGENCY] CRITICAL TEMPERATURE HIT! Forcing fan to 100% (255)...\n\n";
            std::system("sh -c 'echo 255 > $(find /sys/class/hwmon/hwmon*/ -name \"pwm1\" | head -n 1) 2>/dev/null'");
        } 
        else if (old_state == ThermalState::CRITICAL && next_state != ThermalState::CRITICAL) {
            std::cout << "\n[RECOVERY] Safe temperature reached! Turning fan back off (0)...\n\n";
            std::system("sh -c 'echo 125 > $(find /sys/class/hwmon/hwmon*/ -name \"pwm1\" | head -n 1) 2>/dev/null'");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }
    close(fd);
}

//for ctrl C shutdown
void signalHandler(int) {
    if (g_running_ptr) g_running_ptr->store(false);
}

int main(){
    RingBuffer ring_buffer(5);
    std::atomic<float> temperature = 70.0f;
    std::atomic<ThermalState> currentState = ThermalState::NORMAL; 
    std::atomic<bool> running = true;
    std::atomic<int> droppedFrames{0};

    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);


    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera\n";
        return 1;
    }

    // TensorRT setup
    std::vector<char> engineData = readEngineFile("/home/roman_b/Thermal-Aware-AI-Jetson-Project/src/Phase_3_AI/yolov8n.engine");
    nvinfer1::IRuntime* runtime = nvinfer1::createInferRuntime(gLogger);
    nvinfer1::ICudaEngine* engine = runtime->deserializeCudaEngine(engineData.data(), engineData.size());
    nvinfer1::IExecutionContext* context = engine->createExecutionContext();

    if (!runtime || !engine || !context) {
        std::cerr << "Failed to initialize TensorRT\n";
        return 1;
    }

    size_t inputSize = 1 * 3 * 640 * 640 * sizeof(float);
    size_t outputSize = 1 * 84 * 8400 * sizeof(float);
    void* inputDevice;
    void* outputDevice;
    cudaMalloc(&inputDevice, inputSize);
    cudaMalloc(&outputDevice, outputSize);

    context->setTensorAddress("images", inputDevice);
    context->setTensorAddress("output0", outputDevice);

    cudaStream_t stream;
    cudaStreamCreate(&stream);

    std::cout << "TensorRT engine loaded and ready\n";

    //stops when ctrl C
    g_running_ptr = &running;
    std::signal(SIGINT, signalHandler);

    std::thread producer(producer_thread, std::ref(ring_buffer), std::ref(currentState), std::ref(running), std::ref(droppedFrames), std::ref(cap));
    std::thread consumer(consumer_thread, std::ref(ring_buffer), std::ref(currentState), 
        std::ref(running), std::ref(temperature), context, inputDevice, outputDevice, 
        stream, inputSize, outputSize);
    std::thread governor(governor_thread, std::ref(temperature), std::ref(currentState), std::ref(running));

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ring_buffer.notifyShutdown(); //after running == false changes flag so consumer/pop doesnt hang

    producer.join();
    consumer.join();
    governor.join();

    //cleanup
    cudaStreamDestroy(stream);
    cudaFree(inputDevice);
    cudaFree(outputDevice);
    delete context;
    delete engine;
    delete runtime;

    std::system("systemctl start nvfancontrol");

    return 0;
}