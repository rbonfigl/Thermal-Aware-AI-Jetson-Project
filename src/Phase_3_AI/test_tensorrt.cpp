#include <NvInfer.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <cuda_runtime_api.h>

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
    
    std::cout << "Read engine file: " << size << " bytes" << std::endl;
    return buffer;
}

int main() {
    std::vector<char> engineData = readEngineFile("yolov8n.engine");

    nvinfer1::IRuntime* runtime = nvinfer1::createInferRuntime(gLogger);
    if (!runtime) {
        std::cerr << "Failed to create TensorRT runtime" << std::endl;
        return 1;
    }
    std::cout << "Runtime created successfully" << std::endl;

    nvinfer1::ICudaEngine* engine = runtime->deserializeCudaEngine(engineData.data(), engineData.size());
    if (!engine) {
        std::cerr << "Failed to deserialize engine" << std::endl;
        return 1;
    }
    std::cout << "Engine deserialized successfully" << std::endl;

    nvinfer1::IExecutionContext* context = engine->createExecutionContext();
    if (!context) {
        std::cerr << "Failed to create execution context" << std::endl;
        return 1;
    }
    std::cout << "Execution context created successfully" << std::endl;

    // Print input/output tensor info to confirm we understand the model's shape
    int numIOTensors = engine->getNbIOTensors();
    std::cout << "Number of I/O tensors: " << numIOTensors << std::endl;
    
    for (int i = 0; i < numIOTensors; i++) {
        const char* name = engine->getIOTensorName(i);
        nvinfer1::Dims dims = engine->getTensorShape(name);
        
        std::cout << "Tensor " << i << ": " << name << " | Shape: (";
        for (int j = 0; j < dims.nbDims; j++) {
            std::cout << dims.d[j];
            if (j < dims.nbDims - 1) std::cout << ", ";
        }
        std::cout << ")" << std::endl;
    }

    // Load a test image
    cv::Mat img = cv::imread("test_frame.jpg");  // use the frame you saved earlier
    if (img.empty()) {
        std::cerr << "Failed to load test image" << std::endl;
        return 1;
    }

    // Preprocess: resize to 640x640, BGR->RGB, HWC->CHW, normalize to 0-1
    cv::Mat blob = cv::dnn::blobFromImage(img, 1.0/255.0, cv::Size(640, 640), 
                                        cv::Scalar(0,0,0), true, false);
    // blob is now a properly-shaped (1,3,640,640) float array, matching the model's input

    // Allocate GPU memory for input and output
    void* inputDevice;
    void* outputDevice;
    size_t inputSize = 1 * 3 * 640 * 640 * sizeof(float);
    size_t outputSize = 1 * 84 * 8400 * sizeof(float);

    cudaMalloc(&inputDevice, inputSize);
    cudaMalloc(&outputDevice, outputSize);
    cudaStream_t stream;
    cudaStreamCreate(&stream);

    // Copy preprocessed image data from CPU to GPU
    cudaMemcpy(inputDevice, blob.ptr<float>(), inputSize, cudaMemcpyHostToDevice);

    // Tell the context where the input/output buffers are
    context->setTensorAddress("images", inputDevice);
    context->setTensorAddress("output0", outputDevice);

    // Run inference
    context->enqueueV3(stream);  
    cudaStreamSynchronize(stream);

    // Copy results back to CPU
    std::vector<float> output(1 * 84 * 8400);
    cudaMemcpy(output.data(), outputDevice, outputSize, cudaMemcpyDeviceToHost);

    const int numClasses = 80;
    const int numBoxes = 8400;
    const float confThreshold = 0.5f;

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    for (int i = 0; i < numBoxes; i++) {
        // Find the class with highest confidence for this box
        float maxConf = 0.0f;
        int maxClassId = -1;
        for (int c = 0; c < numClasses; c++) {
            float conf = output[(4 + c) * numBoxes + i];
            if (conf > maxConf) {
                maxConf = conf;
                maxClassId = c;
            }
        }

        if (maxConf > confThreshold) {
            float cx = output[0 * numBoxes + i];
            float cy = output[1 * numBoxes + i];
            float w  = output[2 * numBoxes + i];
            float h  = output[3 * numBoxes + i];

            int x = static_cast<int>(cx - w / 2);
            int y = static_cast<int>(cy - h / 2);

            boxes.push_back(cv::Rect(x, y, static_cast<int>(w), static_cast<int>(h)));
            confidences.push_back(maxConf);
            classIds.push_back(maxClassId);
        }
    }

    // Non-max suppression to remove overlapping duplicate boxes
    std::vector<int> nmsIndices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, 0.45f, nmsIndices);

    std::cout << "Detected " << nmsIndices.size() << " objects:" << std::endl;

    // COCO class names (first several, add more if you want full 80)
    std::vector<std::string> classNames = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", 
        "truck", "boat", "traffic light", "fire hydrant", "stop sign", 
        "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow"
        // ... truncated; add remaining COCO classes if you want full label coverage
    };

    for (int idx : nmsIndices) {
        std::string label = (classIds[idx] < (int)classNames.size()) 
            ? classNames[classIds[idx]] 
            : ("class_" + std::to_string(classIds[idx]));
        std::cout << "  " << label << " | confidence: " << confidences[idx] 
                << " | box: " << boxes[idx] << std::endl;
    }

    // Draw boxes on the image and save it, for visual confirmation
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(640, 640));
    for (int idx : nmsIndices) {
        cv::rectangle(resized, boxes[idx], cv::Scalar(0, 255, 0), 2);
        std::string label = (classIds[idx] < (int)classNames.size()) 
            ? classNames[classIds[idx]] 
            : ("class_" + std::to_string(classIds[idx]));
        cv::putText(resized, label, cv::Point(boxes[idx].x, boxes[idx].y - 5), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
    }
    cv::imwrite("detection_result.jpg", resized);
    std::cout << "Saved detection_result.jpg" << std::endl;

    cudaFree(inputDevice);
    cudaFree(outputDevice);
    cudaStreamDestroy(stream);
    // Cleanup
    delete context;
    delete engine;
    delete runtime;

    std::cout << "Cleanup complete" << std::endl;
    return 0;
}