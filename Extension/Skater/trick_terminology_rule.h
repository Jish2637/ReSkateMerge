#pragma once
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace dingosdk::trick_terminology {
// A field belongs to us only while its current contents equal our last write.
// Native revisions supersede the saved value independently for each label.
struct TextEdit {
    std::string original, written;
    bool active{}, pending{};
    std::string pending_before;
    // Explicit producer input is authoritative, even if equal to our old alias.
    void incoming(const std::string& native, const std::string& physical) {
        original=native;active=false;pending=true;pending_before=physical;
    }
    void publication_finished(const std::string& physical) {
        pending=false;
        (void)observe(physical);
    }
    std::string observe(const std::string& current) {
        if (active && current == written) return original;
        if (pending && current == pending_before) return original;
        original = current;
        active = false;
        return current;
    }
    void applied(const std::string& before, const std::string& after) {
        original = before; written = after; active = true;
    }
};
inline std::string normalized(std::string_view text) {
    std::string out;
    for (unsigned char c : text)
        if (c < 128 && std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}
// BoardDirection is not body-spin direction. Only accept an explicit BS/FS
// token from the separate metadata or formatted skater-rotation text.
inline std::string body_direction(std::string_view metadata, std::string_view rotation_text) {
    const auto meta = normalized(metadata), rotation = normalized(rotation_text);
    const auto from_meta = meta == "bs" ? "BS" : meta == "fs" ? "FS" : "";
    const auto from_rotation = rotation == "bs180" ? "BS" : rotation == "fs180" ? "FS" : "";
    if (*from_meta && *from_rotation && std::string_view(from_meta) != from_rotation) return {};
    return *from_meta ? from_meta : from_rotation;
}
// base, explicit body direction, body rotation, then excluded modifiers.
inline bool ghetto_bird(const std::array<std::string, 13>& labels) {
    if (normalized(labels[0]) != "hardflip" || normalized(labels[1]) != "bs" ||
        normalized(labels[2]) != "180") return false;
    for (std::size_t i = 3; i < labels.size(); ++i)
        if (!labels[i].empty()) return false;
    return true;
}
}
