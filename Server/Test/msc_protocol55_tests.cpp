#include "Extension/Multiplayer/Net/protocol.h"
#include <iostream>
#include <stdexcept>
using namespace dingosdk::multiplayer;
static void require(bool yes, const char* what) { if(!yes) throw std::runtime_error(what); }
int main() {
    try {
        require(protocol_version == 55, "Not upstream protocol 55");
        require(static_cast<unsigned>(PacketKind::present) == 37 && static_cast<unsigned>(PacketKind::msc) == 38,
                "Packet IDs conflict");
        Packet p; p.kind = PacketKind::msc; p.source=76561198000000001ULL;
        p.session=1; p.epoch=2; p.world=1; p.map=3;
        p.text = "{\"v\":1,\"test\":\"hello\"}";
        const auto out=decode(encode(p));
        require(out && out->kind==PacketKind::msc && out->text==p.text, "MSC roundtrip failed");
        p.kind=PacketKind::present; p.text.clear();
        const auto up=decode(encode(p));
        require(up && up->kind==PacketKind::present && up->text.empty(), "Upstream presence roundtrip failed");
        p.kind=PacketKind::msc; p.text="";
        bool rejects=false;
        try { (void)encode(p); } catch (const std::invalid_argument&) { rejects=true; }
        require(rejects, "Empty MSC payload was accepted");
        std::cout << "MSC protocol-55 packet 38 / upstream presence packet 37: PASS\n";
        return 0;
    } catch (const std::exception& ex) {std::cerr << ex.what() << '\n'; return 1;}
}
