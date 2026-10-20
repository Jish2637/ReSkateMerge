#include "Engine/Game/Input/controller_bindings.h"
#include "Engine/Game/Input/playstation_report.h"
#include "Engine/Game/Input/playstation_capture.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace dingosdk;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        check(playstation_pad(0x054c, 0x09cc) == PlayStationPad::dualshock4, "DualShock 4 v2 is recognised");
        check(playstation_pad(0x054c, 0x0ce6) == PlayStationPad::dualsense, "DualSense is recognised");
        check(playstation_pad(0x054c, 0x0df2) == PlayStationPad::dualsense, "USB DualSense Edge is recognised");
        check(playstation_pad(0x045e, 0x0ce6) == PlayStationPad::none, "Other vendors are ignored");
        check(!playstation_bluetooth(64) && playstation_bluetooth(78) && playstation_bluetooth(547), "Transport follows the input report length");

        // DS4 USB 0x01: sticks centred, hat NE, Cross, L1 + Options, L2 fully pressed.
        std::array<std::uint8_t, 64> ds4{0x01, 128, 128, 128, 128, 0x21, 0x21, 0x00, 255, 0};
        auto pad = parse_playstation_report(PlayStationPad::dualshock4, false, ds4.data(), ds4.size());
        check(pad && pad->buttons == (0x1 | 0x8 | 0x1000 | 0x100 | 0x10), "DS4 USB buttons map to XInput bits");
        check(pad->left_trigger == 255 && pad->right_trigger == 0, "DS4 USB triggers are analog");
        check(pad->left_x == 0 && pad->left_y == -258, "Centred sticks stay near zero");
        ds4[5] = 0x08; ds4[1] = 255; ds4[2] = 0;
        pad = parse_playstation_report(PlayStationPad::dualshock4, false, ds4.data(), ds4.size());
        check(pad && pad->buttons == 0x100 + 0x10 && pad->left_x > 32700 && pad->left_y > 32700, "Neutral hat and full up-right stick");

        // DS4 Bluetooth full 0x11: layout shifted by two, Triangle + R3.
        std::array<std::uint8_t, 547> bt{0x11, 0xc0, 0x00, 128, 128, 128, 128, 0x88, 0x80, 0, 0, 200};
        pad = parse_playstation_report(PlayStationPad::dualshock4, true, bt.data(), bt.size());
        check(pad && pad->buttons == (0x8000 | 0x80) && pad->right_trigger == 200, "DS4 Bluetooth full report");

        // DualSense USB 0x01: triggers at [5]/[6], buttons at [8]/[9]. Circle + Create + R1.
        std::array<std::uint8_t, 64> ds5{0x01, 128, 128, 128, 128, 0, 90, 0x17, 0x48, 0x12};
        pad = parse_playstation_report(PlayStationPad::dualsense, false, ds5.data(), ds5.size());
        check(pad && pad->buttons == (0x2000 | 0x20 | 0x200) && pad->right_trigger == 90, "DualSense USB report");

        // DualSense Bluetooth: the full 0x31 report, and the simple 0x01 report padded to 78 bytes.
        std::array<std::uint8_t, 78> ds5bt{0x31, 0x10, 128, 128, 128, 128, 40, 0, 0, 0x14, 0x20};
        pad = parse_playstation_report(PlayStationPad::dualsense, true, ds5bt.data(), ds5bt.size());
        check(pad && pad->buttons == (0x2 | 0x4000 | 0x10) && pad->left_trigger == 40, "DualSense Bluetooth full report");
        std::array<std::uint8_t, 78> simple{0x01, 128, 128, 128, 128, 0x28, 0x02, 0, 0, 0};
        pad = parse_playstation_report(PlayStationPad::dualsense, true, simple.data(), simple.size());
        check(pad && pad->buttons == (0x1000 | 0x200), "DualSense Bluetooth simple report uses the DS4 layout");
        check(!parse_playstation_report(PlayStationPad::dualsense, false, simple.data(), 5), "Short reports are rejected");

        // The affected USB DualSense Edge returned 192 bytes into a 2048-byte
        // buffer: three 64-byte reports, not one Bluetooth simple report.
        for (const auto batch_count : {1u, 2u, 3u, 8u, 32u}) {
            std::array<std::uint8_t, 2048> transfer;
            transfer.fill(0xa5);
            for (unsigned i = 0; i < batch_count; ++i) {
                std::copy(ds5.begin(), ds5.end(), transfer.begin() + i * ds5.size());
                transfer[i * ds5.size() + 7] = static_cast<std::uint8_t>(i + 1); // sequence
            }
            const auto bytes = batch_count * ds5.size();
            const auto reports = visit_playstation_reports(PlayStationPad::dualsense, ds5.size(),
                std::span(transfer).first(bytes), [&](auto report) {neutralize_playstation_report(PlayStationPad::dualsense, report);});
            check(reports == batch_count, "USB completion was not split into individual reports");
            for (unsigned i = 0; i < batch_count; ++i) {
                const auto* report = transfer.data() + i * ds5.size();
                const auto neutral = parse_playstation_report(PlayStationPad::dualsense, false, report, ds5.size());
                check(neutral && !neutral->buttons && !neutral->left_trigger && !neutral->right_trigger,
                    "Menu/chat capture generated a USB button or left a later report pressed");
                check(report[8] == 8 && report[7] == i + 1, "USB hat or report sequence was overwritten incorrectly");
                check(report[1] == 128 && report[2] == 128 && report[3] == 128 && report[4] == 128,
                    "USB stick was not neutralized");
            }
            check(std::all_of(transfer.begin() + bytes, transfer.end(), [](auto byte) {return byte == 0xa5;}),
                "Capture wrote past completed bytes into spare buffer capacity");
            const auto once = transfer;
            visit_playstation_reports(PlayStationPad::dualsense, ds5.size(), std::span(transfer).first(bytes),
                [&](auto report) {neutralize_playstation_report(PlayStationPad::dualsense, report);});
            check(transfer == once, "Repeated completion processing changed a neutral USB report");
        }
        // Bluetooth full/simple and DS4 layouts must still remain neutral.
        const auto capture_batch = [](PlayStationPad kind, const auto& sample, unsigned count) {
            std::vector<std::uint8_t> transfer;
            for (unsigned i = 0; i < count; ++i) transfer.insert(transfer.end(), sample.begin(), sample.end());
            check(visit_playstation_reports(kind, sample.size(), transfer, [&](auto report) {
                const auto protocol = report[0];
                neutralize_playstation_report(kind, report);
                const auto neutral = parse_playstation_report(kind, playstation_bluetooth(sample.size()), report.data(), report.size());
                check(neutral && !neutral->buttons && !neutral->left_trigger && !neutral->right_trigger,
                    "Capture generated a Bluetooth/DS4 button");
                check(report[0] == protocol, "Capture changed the report ID");
                const std::vector<std::uint8_t> once(report.begin(), report.end());
                update_playstation_report_crc(report);
                check(std::equal(report.begin(), report.end(), once.begin()), "Bluetooth checksum was not refreshed");
            }) == count, "Bluetooth/DS4 batch was not fully processed");
        };
        capture_batch(PlayStationPad::dualsense, ds5bt, 2);
        capture_batch(PlayStationPad::dualsense, simple, 3);
        capture_batch(PlayStationPad::dualshock4, ds4, 3);
        capture_batch(PlayStationPad::dualshock4, bt, 2);
        auto untouched = ds5;
        unsigned calls{};
        const auto consume = [&](auto) {++calls;};
        check(!visit_playstation_reports(PlayStationPad::dualsense, 0, untouched, consume), "Unknown stride was guessed");
        check(!visit_playstation_reports(PlayStationPad::dualsense, 64, std::span(untouched).first(63), consume), "Partial report was edited");
        check(!visit_playstation_reports(PlayStationPad::none, 64, untouched, consume), "Unknown device was edited");
        check(!calls && untouched == ds5, "Rejected transfer was changed");
        std::array<std::uint8_t, 128> malformed;
        std::copy(ds5.begin(), ds5.end(), malformed.begin());
        std::copy(ds5.begin(), ds5.end(), malformed.begin() + ds5.size());
        malformed[64] = 0xff;
        const auto before_reject = malformed;
        check(!visit_playstation_reports(PlayStationPad::dualsense, 64, malformed, consume) && !calls && malformed == before_reject,
            "Malformed later report caused a partially modified batch");

        check(controller_combo_label(0x300) == "LB + RB", "Xbox labels");
        check(controller_combo_label(0x300 | 0x1000, ControllerStyle::dualshock4) == "L1 + R1 + Cross", "PlayStation labels");
        check(controller_combo_label(0x20, ControllerStyle::dualsense) == "Create" &&
            controller_combo_label(0x20, ControllerStyle::dualshock4) == "Share", "Back button names per pad");
        std::puts("PlayStation report checks passed."); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
