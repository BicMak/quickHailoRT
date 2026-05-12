#include "hailo_infer.hpp"
#include "logging.hpp"
#include <thread>

using namespace hailort;


HailoInfer::HailoInfer(const std::vector<HailoModel> &configs) {
    LOG_TRACE("creating VDevice");
    this->vdevice = VDevice::create().expect("Failed to create VDevice");
    auto physical_devices = this->vdevice->get_physical_devices().expect("Failed to get physical devices");
    this->device = &physical_devices[0].get();
    LOG_TRACE("VDevice created");

    for (const auto &cfg : configs) {
        LOG_TRACE("loading HEF: %s", cfg.hef_path.c_str());
        auto hef = Hef::create(cfg.hef_path).expect("Failed to create HEF");

        auto configure_params = this->vdevice->create_configure_params(hef).expect("Failed to create configure params");
        auto network_groups   = this->vdevice->configure(hef, configure_params).expect("Failed to configure network group");

        HailoModelState state;
        state.config        = cfg;
        state.network_group = network_groups[0];

        for (auto &info : hef.get_output_vstream_infos().expect("Failed to get output vstream infos")) {
            state.output_vstream_info_by_name[std::string(info.name)] = info;
            LOG_TRACE("output vstream registered: %s", info.name);
        }

        init_vstreams(state);
        //state를 옮겨줄때는 소유권을 옮겨줘야함. vstream 버퍼는 중복생성이 안됨
        this->models.push_back(std::move(state));
        LOG_TRACE("model loaded: %s", cfg.hef_path.c_str());
    }

    LOG_TRACE("HailoInfer init done: %zu model(s)", this->models.size());
}

HailoInfer::HailoInfer(const std::string &hef_path,
                       hailo_format_type_t input_type,
                       hailo_format_type_t output_type)
    : HailoInfer(std::vector<HailoModel>{{hef_path, input_type, output_type}}) {}

void HailoInfer::init_vstreams(HailoModelState &state) {
    const auto input_type  = state.config.input_type;
    const auto output_type = state.config.output_type;

    LOG_TRACE("creating input vstream params (format_type=%d)", static_cast<int>(input_type));
    auto input_params = state.network_group->make_input_vstream_params(
        {}, input_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create input vstream params");

    state.input_vstreams = VStreamsBuilder::create_input_vstreams(
        *state.network_group, input_params
    ).expect("Failed to create input vstreams");
    for (auto &vs : state.input_vstreams) {
        auto info = vs.get_info();
        LOG_TRACE("input vstream: name=%s shape=[H:%u W:%u C:%u] frame_size=%zu",
                  vs.name().c_str(),
                  info.shape.height, info.shape.width, info.shape.features,
                  vs.get_frame_size());
    }

    LOG_TRACE("creating output vstream params (format_type=%d)", static_cast<int>(output_type));
    auto output_params = state.network_group->make_output_vstream_params(
        {}, output_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create output vstream params");

    state.output_vstreams = VStreamsBuilder::create_output_vstreams(
        *state.network_group, output_params
    ).expect("Failed to create output vstreams");
    for (auto &vs : state.output_vstreams) {
        auto info = vs.get_info();
        LOG_TRACE("output vstream: name=%s shape=[H:%u W:%u C:%u] frame_size=%zu",
                  vs.name().c_str(),
                  info.shape.height, info.shape.width, info.shape.features,
                  vs.get_frame_size());
    }
}

hailo_vstream_info_t HailoInfer::get_input_info(size_t model_index) const {
    auto info = this->models[model_index].input_vstreams[0].get_info();
    LOG_TRACE("get_input_info[%zu]: name=%s shape=[H:%u W:%u C:%u]",
              model_index, info.name,
              info.shape.height, info.shape.width, info.shape.features);
    return info;
}

std::vector<hailo_vstream_info_t> HailoInfer::get_output_infos(size_t model_index) const {
    std::vector<hailo_vstream_info_t> infos;
    for (auto &vstream : this->models[model_index].output_vstreams) {
        infos.push_back(vstream.get_info());
        LOG_TRACE("get_output_infos[%zu]: name=%s shape=[H:%u W:%u C:%u]",
                  model_index, infos.back().name,
                  infos.back().shape.height,
                  infos.back().shape.width,
                  infos.back().shape.features);
    }
    return infos;
}

hailo_format_type_t HailoInfer::get_input_type(size_t model_index) const {
    return this->models[model_index].config.input_type;
}

hailo_format_type_t HailoInfer::get_output_type(size_t model_index) const {
    return this->models[model_index].config.output_type;
}

void HailoInfer::set_input_buffers(HailoModelState &state,
                                   const hailort::MemoryView &input,
                                   hailo_status &status) {
    status = state.input_vstreams[0].write(input);
}

hailo_status HailoInfer::infer(
    const hailort::MemoryView &input,
    std::vector<hailort::MemoryView> &output_buffers,
    size_t model_index) {

    auto &state = this->models[model_index];

    if (output_buffers.size() != state.output_vstreams.size()) {
        LOG_ERROR("output buffer count mismatch: got %zu, expected %zu",
                  output_buffers.size(), state.output_vstreams.size());
        return HAILO_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < state.output_vstreams.size(); ++i) {
        size_t expected = state.output_vstreams[i].get_frame_size();
        if (output_buffers[i].size() != expected) {
            LOG_ERROR("output_buffers[%zu] size mismatch: got %zu, expected %zu",
                      i, output_buffers[i].size(), expected);
            return HAILO_INVALID_ARGUMENT;
        }
    }

    LOG_TRACE("infer start: model_index=%zu input_size=%zu output_type=%d output_vstreams=%zu",
              model_index, input.size(),
              static_cast<int>(state.config.output_type),
              state.output_vstreams.size());

    hailo_status write_status = HAILO_SUCCESS;
    std::thread write_thread([this, &state, input, &write_status]() {
        set_input_buffers(state, input, write_status);
    });

    std::vector<hailo_status> read_statuses(state.output_vstreams.size(), HAILO_SUCCESS);
    std::vector<std::thread> read_threads;
    for (size_t i = 0; i < state.output_vstreams.size(); ++i) {
        read_threads.emplace_back([&state, &output_buffers, i, &read_statuses]() {
            read_statuses[i] = state.output_vstreams[i].read(output_buffers[i]);
        });
    }

    write_thread.join();
    for (auto &t : read_threads)
        t.join();
    LOG_TRACE("all threads joined");

    if (HAILO_SUCCESS != write_status)
        LOG_ERROR("write thread failed: status=%d", static_cast<int>(write_status));
    for (size_t i = 0; i < read_statuses.size(); ++i) {
        if (HAILO_SUCCESS != read_statuses[i])
            LOG_ERROR("read thread[%zu] failed: status=%d", i, static_cast<int>(read_statuses[i]));
    }

    auto temp = this->device->get_chip_temperature();
    if (temp) LOG_INFO("temperature: ts0=%.1f C ts1=%.1f C", temp->ts0_temperature, temp->ts1_temperature);

    LOG_TRACE("infer done: model_index=%zu", model_index);
    return write_status != HAILO_SUCCESS ? write_status
         : *std::max_element(read_statuses.begin(), read_statuses.end());
}
