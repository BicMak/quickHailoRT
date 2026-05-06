#include "hailo_infer.hpp"
#include <cstring>
#include <iostream>
#include <thread>


HailoInfer::HailoInfer(const std::string &hef_path,
                       hailo_format_type_t input_type,
                       hailo_format_type_t output_type){
    this->vdevice = hailort::VDevice::create().expect("Failed to create VDevice");

    auto hef = hailort::Hef::create(hef_path).expect("Failed to create HEF");

    auto configure_params = this->vdevice->create_configure_params(hef).expect("Failed to create configure params");
    auto network_groups = this->vdevice->configure(hef, configure_params).expect("Failed to configure network group");
    if (network_groups.size() != 1) {
        throw std::runtime_error("Invalid amount of network groups");
    }
    this->network_group = network_groups[0];

    for (auto &info : hef.get_output_vstream_infos().expect("Failed to get output vstream infos")) {
        this->output_vstream_info_by_name[std::string(info.name)] = info;
    }

    init_vstreams(input_type, output_type);
}

void HailoInfer::init_vstreams(
    hailo_format_type_t input_type,
    hailo_format_type_t output_type){
    // 1. make input vstream
    auto input_params = this->network_group->make_input_vstream_params(
        {}, input_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create input vstream params");

    this->input_vstreams = VStreamsBuilder::create_input_vstreams(
        *this->network_group, input_params
    ).expect("Failed to create input vstreams");

    // 2. make output vstream
    auto output_params = this->network_group->make_output_vstream_params(
        {}, output_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE
    ).expect("Failed to create output vstream params");

    this->output_vstreams = VStreamsBuilder::create_output_vstreams(
        *this->network_group, output_params
    ).expect("Failed to create output vstreams");
}

hailo_vstream_info_t HailoInfer::get_input_info() {
    return this->input_vstreams[0].get_info();
}

std::vector<hailo_vstream_info_t> HailoInfer::get_output_infos() {
    std::vector<hailo_vstream_info_t> infos;
    for (auto &vstream : this->output_vstreams) {
        infos.push_back(vstream.get_info());
    }
    return infos;
}

void HailoInfer::set_input_buffers(const cv::Mat &input, hailo_status &status){
    auto &vstream = this->input_vstreams[0];
    size_t frame_size = vstream.get_frame_size();
    status = vstream.write(MemoryView(input.data, frame_size));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed to write to input vstream, status = " << status << std::endl;
    }
}

std::vector<std::pair<uint8_t*, hailo_vstream_info_t>> HailoInfer::infer(
    cv::Mat &input,
    std::vector<std::vector<uint8_t>> &output_buffers){
    // 출력 스트림 수만큼 버퍼 슬롯 확보.
    // 각 슬롯 크기는 해당 vstream이 요구하는 1프레임 바이트 수 (포맷/해상도 기반).
    // output_buffers는 호출자가 소유 — 반환된 uint8_t* 포인터들의 수명이 이 벡터에 묶임.
    output_buffers.clear();
    output_buffers.reserve(this->output_vstreams.size());
    for (auto &vstream : this->output_vstreams) {
        output_buffers.emplace_back(vstream.get_frame_size());
    }

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

    if (HAILO_SUCCESS != write_status)
        std::cerr << "Write thread failed, status = " << write_status << std::endl;
    for (size_t i = 0; i < read_statuses.size(); ++i) {
        if (HAILO_SUCCESS != read_statuses[i])
            std::cerr << "Read thread[" << i << "] failed, status = " << read_statuses[i] << std::endl;
    }

    // 결과 조립: 각 출력 버퍼의 raw 포인터 + 해당 vstream의 메타데이터(shape, format 등)를 페어로 묶음.
    // 호출자는 이 포인터로 후처리(bbox decode, NMS 등)를 수행.
    std::vector<std::pair<uint8_t*, hailo_vstream_info_t>> results;
    for (size_t i = 0; i < this->output_vstreams.size(); ++i) {
        results.emplace_back(output_buffers[i].data(),
                             output_vstream_info_by_name.at(this->output_vstreams[i].name()));
    }
    return results;
}
