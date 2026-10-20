#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <deque>
namespace dingosdk::first_person_integration {
// Semantic offsets verified through DirectInput object usage metadata. Never
// infer a controller layout from buffer length or modify unrecognized devices.
struct DirectButtonFilter {
    struct Field {std::uint32_t offset{},mask{};bool pov{};};
    struct Event {std::uint32_t offset{},data{},time{},sequence{},edited{};};
    std::vector<Field> fields;
    std::uint32_t held{};
    std::uint64_t generation{};
    std::deque<Event> events;
    static std::uint32_t dpad(std::uint32_t value) {
        constexpr std::uint32_t bits[]{1,9,8,10,2,6,4,5};
        return (value&0xffff)==0xffff || value>=36000 ? 0:bits[((value+2250)/4500)%8];
    }
    static std::uint32_t pov(std::uint32_t value) {
        constexpr std::uint32_t angles[]{0xffffffffu,0,18000,0xffffffffu,27000,31500,22500,0xffffffffu,
            9000,4500,13500,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
        return angles[value&15];
    }
    void reset() {held=0;events.clear();}
    void sync(std::uint64_t current) {if(current!=generation) {reset();generation=current;}}
    template<class Classify> void state(std::span<std::uint8_t> bytes,Classify classify) {
        held=0;
        for(const auto& field:fields) {
            if(field.offset>=bytes.size()) continue;
            if(field.pov && bytes.size()-field.offset>=4) {std::uint32_t value;std::memcpy(&value,bytes.data()+field.offset,4);held|=dpad(value);}
            else if(!field.pov && (bytes[field.offset]&0x80)) held|=field.mask;
        }
        const auto consume=classify(held);
        for(const auto& field:fields) if(field.pov && field.offset<=bytes.size() && bytes.size()-field.offset>=4 && consume) {
            std::uint32_t value;std::memcpy(&value,bytes.data()+field.offset,4);
            const auto edited=pov(dpad(value)&~consume);std::memcpy(bytes.data()+field.offset,&edited,4);
        }
    }
    template<class Classify> std::uint32_t event(std::uint32_t offset,std::uint32_t data,std::uint32_t time,std::uint32_t sequence,Classify classify) {
        for(const auto& cached:events) if(cached.offset==offset && cached.data==data && cached.time==time && cached.sequence==sequence) return cached.edited;
        auto edited=data;
        for(const auto& field:fields) if(field.offset==offset) {
            if(field.pov) held=(held&~15u)|dpad(data);
            else if(data&0x80) held|=field.mask;else held&=~field.mask;
            const auto consume=classify(held,time);
            if(field.pov && consume) edited=pov(dpad(data)&~consume);
            break;
        }
        if(events.size()>=256) events.pop_front();
        events.push_back({offset,data,time,sequence,edited});return edited;
    }
};
}
