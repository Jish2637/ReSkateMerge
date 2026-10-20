#include "Extension/Skater/FirstPerson/native_button_filter.h"
#include "Engine/Game/Input/playstation_capture.h"
#include "Extension/Skater/FirstPerson/direct_button_filter.h"
#include <iostream>
#include <stdexcept>
using namespace dingosdk;
using namespace dingosdk::first_person_integration;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {try {
    first_person_vendor::FirstPersonLookControls controls;
    for(unsigned i=0;i<4;++i) controls[i].combo=0x100|(1u<<i);
    NativeButtonFilter filter;
    auto publish=[&](bool glance=true,unsigned device=1,std::uintptr_t world=99) {filter.publish(controls,0x302,true,glance,world,device,false,1000);};
    publish();
    check(filter.classify(0,0,0x1101,1000).consume==1,"Glance single press leaked emote");
    check(filter.classify(0,0,0x1001,1010).consume==1,"Shoulder-first glance release leaked D-pad");
    filter.reset();publish();filter.classify(0,0,0x101,1000);
    filter.classify(0,0,0x100,1020);
    check(filter.classify(0,0,0x101,1030).emote && filter.forwarding(1030),"Glance double tap failed to forward emote");
    filter.classify(0,0,0x100,1040);check(!filter.forwarding(1040),"Emote release stuck");
    for(bool entering:{true,false}) {
        filter.reset();publish(!entering);
        check(filter.classify(0,0,0x1302,1000).consume==2,"Native toggle emote leaked");
        publish(entering);check(filter.classify(0,0,0x1002,1010).consume==2,"Camera handover lost held toggle");
        check(filter.classify(0,0,2,5000).consume==2,"Producer stall or shoulder release lost toggle latch");
        filter.classify(0,0,0,5010);check(filter.classify(0,0,0x302,5020).consume==2,"Second toggle became an emote");
        check(!filter.classify(0,1,0x302,5020).consume,"Blocker leaked to another controller");
    }
    filter.reset();publish();filter.classify(0,0,0x101,1000);filter.classify(0,0,0x100,1010);
    check(!filter.classify(0,0,0x101,1200).consume,"Expired glance eligibility remained active");
    filter.reset();publish();filter.classify(0,0,0x302,1000);publish(true,1,100);
    check(!filter.classify(0,0,2,1000).consume,"New world inherited consumed toggle");
    filter.classify(0,0,0x302,1000);publish(true,2,100);
    check(!filter.classify(0,1,2,1000).consume,"New device inherited toggle");
    filter.reset();publish();filter.classify(0,0,0x302,1000);filter.reset();publish();
    check(!filter.classify(0,0,2,1000).consume,"Menu/focus reset retained stale latch");
    filter.reset();publish();filter.classify(0,0,0x101,1000);filter.classify(0,0,0x100,1010);
    filter.publish(controls,0x302,true,true,99,1,false,1020,{1,2,3,4});
    check(!filter.classify(0,0,0x101,1030).emote,"New owner inherited glance double-tap state");
    for(auto kind:{PlayStationPad::dualshock4,PlayStationPad::dualsense}) for(bool bt:{false,true}) {
        filter.reset();filter.publish(controls,0x302,true,false,99,16,true,1000);
        std::vector<std::uint8_t> report(bt?78:64,0);report[0]=bt?(kind==PlayStationPad::dualsense?0x31:0x11):1;
        const unsigned start=bt?(kind==PlayStationPad::dualsense?2:3):1;
        const unsigned button=start+(kind==PlayStationPad::dualsense?7:4);
        for(unsigned i=0;i<4;++i) report[start+i]=128;
        report[start+2]=211;report[button]=0x24;report[button+1]=3;
        check(filter.filter_report(1,7,kind,report,1000),"Sony native toggle not consumed");
        check(report[button]==0x28 && report[button+1]==3 && report[start+2]==211,"Sony edit damaged other buttons or axes");
        auto edited=report;filter.filter_report(1,7,kind,report,1001);check(edited==report,"Duplicate completion changed report");
        report[button]=0x24;report[button+1]=0;report[start+6]++;
        check(filter.filter_report(1,7,kind,report,4000) && report[button]==0x28,"Sony held D-pad escaped after shoulders released");
        if(bt) {edited=report;update_playstation_report_crc(report);check(edited==report,"Sony Bluetooth CRC stale");}
        report[button]=0x28;report[start+6]++;filter.filter_report(1,7,kind,report,4010);
        report[button]=0x24;report[start+6]++;
        check(!filter.filter_report(1,7,kind,report,4020),"Unmodified D-pad press blocked after release");
    }
    // USB completions remain split by the actual HID report length.
    std::vector<std::uint8_t> batch(192,0);
    for(unsigned i=0;i<3;++i) {batch[i*64]=1;batch[i*64+8]=4;batch[i*64+9]=3;batch[i*64+7]=static_cast<std::uint8_t>(i);}
    filter.reset();filter.publish(controls,0x302,true,false,99,16,true,1000);
    check(visit_playstation_reports(PlayStationPad::dualsense,64,batch,[&](auto report){filter.filter_report(1,8,PlayStationPad::dualsense,report,1000);})==3,"USB batch was not split");
    for(unsigned i=0;i<3;++i) check(batch[i*64+8]==8 && batch[i*64+9]==3,"USB batch toggle leaked");
    // Verified DirectInput metadata can use arbitrary offsets. State and
    // buffered/peek reads share the same native toggle policy and preserve axes.
    DirectButtonFilter direct;direct.fields={{8,0x100,false},{12,0x200,false},{24,0,true}};
    filter.reset();filter.publish(controls,0x302,true,false,99,16,true,1000);
    auto classify=[&](std::uint32_t buttons,std::uint64_t time=0){return filter.classify(3,123,buttons,1000,time).consume;};
    std::vector<std::uint8_t> state(40,0x55);state[8]=state[12]=0x80;
    std::uint32_t down=18000;std::memcpy(state.data()+24,&down,4);direct.state(state,classify);
    std::uint32_t hat{};std::memcpy(&hat,state.data()+24,4);
    check(hat==0xffffffffu && state[8]==0x80 && state[12]==0x80 && state[0]==0x55,"DirectInput state leaked or damaged unrelated data");
    direct.reset();filter.reset();filter.publish(controls,0x302,true,false,99,16,true,1000);
    direct.event(8,0x80,1,1,classify);direct.event(12,0x80,2,2,classify);
    check(direct.event(24,18000,3,3,classify)==0xffffffffu,"DirectInput buffered toggle leaked");
    direct.event(8,0,4,4,classify);direct.event(12,0,5,5,classify);
    check(direct.event(24,18000,3,3,classify)==0xffffffffu,"Repeated DirectInput peek advanced policy");
    direct.event(24,0xffffffffu,6,6,classify);
    check(direct.event(24,18000,7,7,classify)==18000,"DirectInput ordinary D-pad stayed blocked");
    direct.sync(42);check(direct.held==0 && direct.events.empty(),"DirectInput retained old world events");
    filter.reset();filter.publish(controls,0,true,true,99,16,true,1000);
    direct.event(8,0x80,10,10,classify);direct.event(24,0,20,20,classify);
    direct.event(24,0xffffffffu,30,30,classify);
    check(direct.event(24,0,400,400,classify)==0xffffffffu,"Buffered events used poll time instead of physical tap time");
    std::cout<<"Native glance/toggle protection and Sony report regressions passed\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
