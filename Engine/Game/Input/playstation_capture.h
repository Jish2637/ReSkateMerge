#pragma once
#include "playstation_report.h"

namespace dingosdk {
// ReadFile can return several HID reports in one completion. The device's
// InputReportByteLength, not the total transfer or buffer capacity, defines
// both the stride and the transport layout of report 0x01.
template<class Consumer>
std::size_t visit_playstation_reports(PlayStationPad kind, std::size_t report_length,
    std::span<std::uint8_t> transfer, Consumer&& consume) {
    if (report_length < 10 || transfer.empty() || transfer.size() % report_length) return 0;
    const bool bluetooth = playstation_bluetooth(report_length);
    // Reject an unrecognized/partial batch before modifying any report.
    for (std::size_t offset = 0; offset < transfer.size(); offset += report_length)
        if (!parse_playstation_report(kind, bluetooth, transfer.data() + offset, report_length)) return 0;
    for (std::size_t offset = 0; offset < transfer.size(); offset += report_length)
        consume(transfer.subspan(offset, report_length));
    return transfer.size() / report_length;
}

// Called only for a validated individual report. Preserve counters, sensors,
// touch data and protocol bytes; clear the controller input fields alone.
inline void neutralize_playstation_report(PlayStationPad kind, std::span<std::uint8_t> report) noexcept {
    const bool bluetooth = playstation_bluetooth(report.size());
    if (!parse_playstation_report(kind, bluetooth, report.data(), report.size())) return;
    const bool sense_full = kind == PlayStationPad::dualsense &&
        (report[0] == 0x31 || (report[0] == 0x01 && !bluetooth));
    const unsigned offset = report[0] == 0x31 ? 2 : report[0] == 0x11 ? 3 : 1;
    auto* data = report.data() + offset;
    std::fill_n(data, 4, std::uint8_t{128});
    if (sense_full) {
        data[4] = data[5] = 0;
        data[7] = 8; data[8] = data[9] = 0;
    } else {
        data[4] = 8; data[5] = 0; data[6] &= 0xfc;
        data[7] = data[8] = 0;
    }
    update_playstation_report_crc(report);
}
}
