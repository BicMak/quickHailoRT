#include "hailo_infer.hpp"
#include "logging.hpp"
#include <iostream>
#include <thread>

using namespace hailort;


HailoInfer::HailoInfer(const std::string &hef_path,
                       hailo_format_type_t input_type,
                       hailo_format_type_t output_type){
    LOG_TRACE("creating VDevice");
    this->vdevice = hailort::VDevice::create().expect("Failed to create VDevice");
    auto physical_devices = this->vdevice->get_physical_devices().expect("Failed to get physical devices");
    this->device = &physical_devices[0].get();
    LOG_TRACE("VDevice created");

    LOG_TRACE("loading HEF: %s", hef_path.c_str());
    auto hef = hailort::Hef::create(hef_path).expect("Failed to create HEF");
    LOG_TRACE("HEF loaded");

    auto configure_params = this->vdevice->create_configure_params(hef).expect("Failed to create configure params");
    auto network_groups = this->vdevice->configure(hef, configure_params).expect("Failed to configure network group");
    if (network_groups.size() != 1) {
        throw std::runtime_error("Invalid amount of network groups");
    }
    this->network_group = network_groups[0];
    LOG_TRACE("network group configured");

    for (auto &info : hef.get_output_vstream_infos().expect("Failed to get output vstream infos")) {
        this->output_vstream_info_by_name[std::string(info.name)] = info;
        LOG_TRACE("output vstream registered: %s", info.name);
    }

    init_vstreams(input_type, output_type);
    LOG_TRACE("HailoInfer init done");
}

void HailoInfer::init_vstreams(
    hailo_format_type_t input_type,
    hailo_format_type_t output_type){
    // 1. make input vstream
    LOG_TRACE("creating input vstream params (format_type=%d)", static_cast<int>(input_type));
    auto input_params = this->network_group->make_input_vstream_params(
        {}, input_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create input vstream params");

    this->input_vstreams = VStreamsBuilder::create_input_vstreams(
        *this->network_group, input_params
    ).expect("Failed to create input vstreams");
    for (auto &vs : this->input_vstreams) {
        auto info = vs.get_info();
        LOG_TRACE("input vstream: name=%s shape=[H:%u W:%u C:%u] frame_size=%zu",
                  vs.name().c_str(),
                  info.shape.height, info.shape.width, info.shape.features,
                  vs.get_frame_size());
    }

    // 2. make output vstream
    LOG_TRACE("creating output vstream params (format_type=%d)", static_cast<int>(output_type));
    auto output_params = this->network_group->make_output_vstream_params(
        {}, output_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create output vstream params");

    this->output_vstreams = VStreamsBuilder::create_output_vstreams(
        *this->network_group, output_params
    ).expect("Failed to create output vstreams");
    for (auto &vs : this->output_vstreams) {
        auto info = vs.get_info();
        LOG_TRACE("output vstream: name=%s shape=[H:%u W:%u C:%u] frame_size=%zu",
                  vs.name().c_str(),
                  info.shape.height, info.shape.width, info.shape.features,
                  vs.get_frame_size());
    }
}

hailo_vstream_info_t HailoInfer::get_input_info() {
    auto info = this->input_vstreams[0].get_info();
    LOG_TRACE("get_input_info: name=%s shape=[H:%u W:%u C:%u]",
              info.name, info.shape.height, info.shape.width, info.shape.features);
    return info;
}

std::vector<hailo_vstream_info_t> HailoInfer::get_output_infos() {
    std::vector<hailo_vstream_info_t> infos;
    for (auto &vstream : this->output_vstreams) {
        infos.push_back(vstream.get_info());
        LOG_TRACE("get_output_infos: name=%s shape=[H:%u W:%u C:%u]",
                  infos.back().name,
                  infos.back().shape.height, infos.back().shape.width, infos.back().shape.features);
    }
    return infos;
}

void HailoInfer::set_input_buffers(const cv::Mat &input, hailo_status &status){
    auto &vstream = this->input_vstreams[0];
    size_t frame_size = vstream.get_frame_size();
    status = vstream.write(MemoryView(input.data, frame_size));
}

std::vector<std::pair<uint8_t*, hailo_vstream_info_t>> HailoInfer::infer(
    cv::Mat &input,
    std::vector<std::vector<uint8_t>> &output_buffers){
    // 출력 스트림 수만큼 버퍼 슬롯 확보.
    // 각 슬롯 크기는 해당 vstream이 요구하는 1프레임 바이트 수 (포맷/해상도 기반).
    // output_buffers는 호출자가 소유 — 반환된 uint8_t* 포인터들의 수명이 이 벡터에 묶임.
    LOG_TRACE("infer start: input=[H:%d W:%d C:%d type:%d] output_vstreams=%zu",
              input.rows, input.cols, input.channels(), input.type(),
              this->output_vstreams.size());
    output_buffers.clear();
    output_buffers.reserve(this->output_vstreams.size());
    for (auto &vstream : this->output_vstreams) {
        output_buffers.emplace_back(vstream.get_frame_size());
    }
    LOG_TRACE("input frame_size=%zu", this->input_vstreams[0].get_frame_size());

    // VStream은 내부적으로 입력 큐와 출력 큐를 각각 가짐.
    // write()와 read()를 같은 스레드에서 순차 실행하면:
    //   write()가 내부 입력 큐가 꽉 찰 경우 블로킹 → read()에 영원히 도달 못함 → 데드락.
    // write 스레드와 read 스레드를 분리해서 동시에 돌려야
    //   write가 블로킹되더라도 read가 출력 큐를 비워줘서 HW가 계속 진행됨.
    hailo_status write_status = HAILO_SUCCESS;
    std::thread write_thread([this, &input, &write_status]() {
        // input(cv::Mat)의 raw 포인터를 MemoryView로 감싸서 vstream에 write.
        // write()는 내부 입력 큐에 자리가 생길 때까지 블로킹.
        set_input_buffers(input, write_status);
    });

    // 출력 vstream이 여러 개일 수 있으므로 (ex. 멀티 헤드 모델) 각각 별도 스레드로 read.
    // read()는 HW가 해당 출력 큐에 결과를 넣을 때까지 블로킹.
    // 결과는 output_buffers[i]에 직접 복사됨 (VStream 내부 큐 → 사용자 버퍼).
    std::vector<hailo_status> read_statuses(this->output_vstreams.size(), HAILO_SUCCESS);
    std::vector<std::thread> read_threads;
    for (size_t i = 0; i < this->output_vstreams.size(); ++i) {
        read_threads.emplace_back([this, i, &output_buffers, &read_statuses]() {
            auto &vstream = this->output_vstreams[i];
            auto &buf = output_buffers[i];
            read_statuses[i] = vstream.read(MemoryView(buf.data(), buf.size()));
        });
    }

    // 모든 스레드가 끝날 때까지 대기.
    // write 완료 = HW가 입력을 가져감 / read 완료 = HW 출력이 output_buffers에 채워짐.
    write_thread.join();
    for (auto &t : read_threads){
        t.join();
    }
    LOG_TRACE("all threads joined");

    // 핫패스 종료 후 메인 스레드에서 한 번에 status/온도 로깅
    LOG_TRACE("write status=%d", static_cast<int>(write_status));
    if (HAILO_SUCCESS != write_status)
        LOG_ERROR("write thread failed: status=%d", static_cast<int>(write_status));
    for (size_t i = 0; i < read_statuses.size(); ++i) {
        LOG_TRACE("read[%zu] status=%d", i, static_cast<int>(read_statuses[i]));
        if (HAILO_SUCCESS != read_statuses[i])
            LOG_ERROR("read thread[%zu] failed: status=%d", i, static_cast<int>(read_statuses[i]));
    }

    auto temp = this->device->get_chip_temperature();
    if (temp) LOG_INFO("temperature: ts0=%.1f C ts1=%.1f C", temp->ts0_temperature, temp->ts1_temperature);

    // 결과 조립: 각 출력 버퍼의 raw 포인터 + 해당 vstream의 메타데이터(shape, format 등)를 페어로 묶음.
    // 호출자는 이 포인터로 후처리(bbox decode, NMS 등)를 수행.
    std::vector<std::pair<uint8_t*, hailo_vstream_info_t>> results;
    for (size_t i = 0; i < this->output_vstreams.size(); ++i) {
        results.emplace_back(output_buffers[i].data(),
                             output_vstream_info_by_name.at(this->output_vstreams[i].name()));
        LOG_TRACE("result[%zu]: name=%s buf_size=%zu",
                  i, this->output_vstreams[i].name().c_str(), output_buffers[i].size());
    }
    LOG_TRACE("infer done: %zu results", results.size());
    return results;
}
