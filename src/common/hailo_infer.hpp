#ifndef _HAILO_ASYNC_INFERENCE_HPP_
#define _HAILO_ASYNC_INFERENCE_HPP_

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/imgcodecs.hpp>

class HailoInfer {
    private:
        std::unique_ptr<hailort::VDevice> vdevice;
        hailort::Device *device = nullptr;
        std::shared_ptr<hailort::ConfiguredNetworkGroup> network_group;
        std::vector<hailort::InputVStream> input_vstreams;
        std::vector<hailort::OutputVStream> output_vstreams;
        std::map<std::string, hailo_vstream_info_t> output_vstream_info_by_name;

        void init_vstreams(hailo_format_type_t input_type, hailo_format_type_t output_type);
        void set_input_buffers(const cv::Mat &input, hailo_status &status);

    public:
        HailoInfer(const std::string &hef_path,
                hailo_format_type_t input_type = HAILO_FORMAT_TYPE_AUTO,
                hailo_format_type_t output_type = HAILO_FORMAT_TYPE_AUTO);

        hailo_vstream_info_t get_input_info();
        std::vector<hailo_vstream_info_t> get_output_infos();

        // output_buffers는 호출자가 소유 — 반환된 포인터의 수명이 이 벡터에 묶임
        std::vector<std::pair<uint8_t*, hailo_vstream_info_t>> infer(
            cv::Mat &input,
            std::vector<std::vector<uint8_t>> &output_buffers);
};
#endif /* _HAILO_ASYNC_INFERENCE_HPP_ */
