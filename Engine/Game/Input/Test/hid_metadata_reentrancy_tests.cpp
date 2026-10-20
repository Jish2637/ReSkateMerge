#include "Extension/UI/Overlay/input_capture.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace dingosdk::overlay::detail {thread_local unsigned overlay_input_access = 0;}
using namespace dingosdk::overlay::detail;
void check(bool value, const char* message) {if (!value) throw std::runtime_error(message);}
int main() try {
    unsigned originals{}, descriptors{};
    std::function<bool()> attributes;
    attributes = [&] {
        // Model the original HID callback succeeding, then notifying capture.
        ++originals;
        observe_hid_metadata([&] {
            ++descriptors;
            check(descriptors == 1, "Descriptor observation recursively entered itself");
            // Steam's descriptor wrapper calls the hooked attributes API again.
            check(attributes(), "Reentrant native attributes result was lost");
        });
        return true;
    };
    check(attributes(), "Top-level attributes result was lost");
    check(originals == 2 && descriptors == 1, "Expected one descriptor query and both original calls");
    check(overlay_input_access == 0, "Metadata query leaked capture bypass");
    {
        OverlayInputAccess independent_reader;
        check(attributes(), "Independent reader lost original attributes result");
        check(originals == 3 && descriptors == 1, "Independent reader re-entered metadata capture");
        check(overlay_input_access == 1, "Nested observer changed the caller's bypass scope");
    }
    try {observe_hid_metadata([] {throw std::runtime_error("mock driver failure");});}
    catch (const std::runtime_error&) {}
    check(overlay_input_access == 0, "Failed descriptor query leaked capture bypass");
    observe_hid_metadata([&] {++descriptors;});
    check(descriptors == 2, "A completed query permanently disabled later discovery");
    std::cout << "Reentrant Steam HID metadata callbacks passed\n";
    return 0;
} catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
