#ifndef _HAILO_ASYNC_INFERENCE_HPP_
#define _HAILO_ASYNC_INFERENCE_HPP_

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>

// caller-facing model config — copyable, used to construct HailoInfer
struct HailoModel {
    std::string hef_path;
    hailo_format_type_t input_type  = HAILO_FORMAT_TYPE_AUTO;
    hailo_format_type_t output_type = HAILO_FORMAT_TYPE_AUTO;
};

// internal: per-model runtime state
struct HailoModelState {
    HailoModel       config;
    std::vector<hailort::InputVStream>  input_vstreams;
    std::vector<hailort::OutputVStream> output_vstreams;
    std::shared_ptr<hailort::ConfiguredNetworkGroup> network_group;
    std::map<std::string, hailo_vstream_info_t> output_vstream_info_by_name;
};

class HailoInfer {
    private:
        std::unique_ptr<hailort::VDevice> vdevice;
        hailort::Device *device = nullptr;
        std::vector<HailoModelState> models;

        void init_vstreams(HailoModelState &state);
        void set_input_buffers(HailoModelState &state, const hailort::MemoryView &input, hailo_status &status);

    public:
        // multi-model constructor
        HailoInfer(const std::vector<HailoModel> &configs);

        // single-model backward-compatible constructor
        HailoInfer(const std::string &hef_path,
                   hailo_format_type_t input_type  = HAILO_FORMAT_TYPE_AUTO,
                   hailo_format_type_t output_type = HAILO_FORMAT_TYPE_AUTO);

        hailo_vstream_info_t get_input_info(size_t model_index = 0) const;
        std::vector<hailo_vstream_info_t> get_output_infos(size_t model_index = 0) const;
        hailo_format_type_t get_input_type(size_t model_index = 0) const;
        hailo_format_type_t get_output_type(size_t model_index = 0) const;

        // output_buffers: caller-owned and pre-allocated
        // infer() fills each buffer in-place via reference
        hailo_status infer(
            const hailort::MemoryView &input,
            std::vector<hailort::MemoryView> &output_buffers,
            size_t model_index = 0);
};

#endif /* _HAILO_ASYNC_INFERENCE_HPP_ */
