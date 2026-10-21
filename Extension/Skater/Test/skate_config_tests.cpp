#include "Extension/Skater/skate_host_preferences.h"
#include "Extension/Skater/skate_detector.h"
#include "Extension/Skater/trick_set.h"
#include "Extension/Throwdowns/throwdown_wire.h"
#include <stdexcept>
#include <chrono>
using namespace dingosdk;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
trick_logger::TrickSetResult rotation(skate::NumericalRules rules,double angle,bool grind) {
    trick_logger::TrickSetTracker tracker{true,true,true,rules};
    trick_logger::SetSample s;s.valid=true;s.entity=1;s.goofy=false;s.state=grind?400:100;s.now=1;s.steps=1;tracker.update(s);
    s.state=201;s.now+=16;++s.steps;tracker.update(s);
    // Keep individual heading changes below the signed-yaw ambiguity at 180 degrees.
    for(int i=0;i<4;++i){s.now+=16;++s.steps;s.yaw+=angle/4;tracker.update(s);}
    return *tracker.stop(s.now+16);
}
}
void configurable_rules_regressions() {
    const skate::NumericalRules defaults;
    for(bool grind:{false,true})for(double angle:{0.,89.,90.,114.999,115.,179.,180.,269.,270.,450.}) {
        const auto got=rotation(defaults,angle,grind);
        const int old=grind && angle+1e-6<115?0:static_cast<int>(std::floor((angle+90)/180))*180;
        check(got.segments.back().degrees==old,"Configured defaults differ from original spin computation");
    }
    check(skate::NumericalRules{0,0,180}.valid() && skate::NumericalRules{180,179,1080}.valid(),"Slider endpoints refused");
    check(rotation({180,90,450},179,true).segments.back().degrees==0 && rotation({180,90,450},180,true).segments.back().degrees==180,"Maximum grind threshold boundary wrong");
    check(rotation({0,90,450},90,true).segments.back().degrees==180,"Minimum grind threshold not applied");
    check(rotation({0,179,450},0.5,false).segments.back().degrees==0 && rotation({0,179,450},1,false).segments.back().degrees==180,"Maximum spin offset boundary wrong");
    auto rules=defaults;rules.grind_minimum=130;
    check(rotation(rules,120,true).segments.back().degrees==0,"Grind minimum did not restrict adjacent spin");
    check(rotation(rules,120,false).segments.back().degrees==180,"Grind minimum changed ordinary air");
    rules=defaults;rules.spin_offset=0;
    check(rotation(rules,179,false).segments.back().degrees==0,"Spin offset did not change rounding");
    check(rotation(rules,180,false).segments.back().degrees==180,"Spin offset changed exact increments");
    for(int limit:{180,450,1080}) {
        trick_logger::TrickSetTracker tracker{true,true,true,{115,90,limit}};
        trick_logger::SetSample s;s.valid=true;s.entity=1;s.state=100;s.now=1;s.steps=1;tracker.update(s);
        s.state=201;s.now+=16;++s.steps;tracker.update(s);tracker.retain_from(1);
        s.state=101;s.now+=16;++s.steps;tracker.update(s);
        for(int travel=15;travel<=limit;travel+=15) {
            s.now+=16;++s.steps;s.yaw=std::remainder(s.yaw+15,360.);auto result=tracker.update(s);
            check(result.has_value()==(travel==limit),"Recovery limit boundary differs from configured travel");
            if(result)check(result->outcome=="excessive_grounded_rotation","Recovery threshold changed outcome");
        }
    }
    skate::Rules frozen{15,true,{115,0,450}};skate::begin(frozen);
    frozen.numerical.spin_offset=179;
    for(int attempt=0;attempt<2;++attempt) {
        skate::turn(true);trick_logger::SetSample s;s.valid=true;s.entity=1;s.state=100;s.goofy=false;s.now=1;s.steps=1;skate::sample(s);
        s.state=201;s.now+=16;++s.steps;skate::sample(s);
        auto row=Json::object();row["name"]="Ollie";row["name_resolved"]=true;row["observation_id"]=1;row["raw_labels"]=Json::object();row["raw_labels"]["base_name"]="ID_TRICK_OLLIE";skate::observe(row);
        s.yaw=100;s.now+=16;++s.steps;skate::sample(s);
        s.state=100;s.now+=16;++s.steps;skate::sample(s);
        s.sequence_ends=1;s.now+=16;++s.steps;skate::sample(s);skate::native_outcome(true,{});
        const auto result=skate::take();check(result && result->landed && result->display=="Ollie","Detector reset or external preference edit changed frozen numeric rules");
    }
    skate::end();
    for(int mask=0;mask<256;++mask)for(int allowed=0;allowed<16;++allowed) {
        skate::HostingPreferences p;p.categories=static_cast<std::uint8_t>(allowed);
        check(p.effective_mask(static_cast<std::uint8_t>(mask))==((mask&240)|(mask&allowed&15)),"Category intersection changed native authority or unsupported bits");
        check(!skate::Rules{p.effective_mask(static_cast<std::uint8_t>(mask)),true}.allows(skate::Category::manuals),"Preferences enabled manuals");
    }
    using namespace multiplayer;
    ThrowdownMessage m;m.kind=ThrowdownMessage::Kind::offer;m.leader=76561198000000010ULL;m.id=7;m.series="ThrowdownSkate";m.placement={1};m.settings={2};m.rules_mask=0x6f;m.rules_revision=skate::rules_format;m.numerical={130,60,720};
    const auto encoded=encode_throwdown(m);check(decode_throwdown(encoded)==m,"Full host rules did not round trip");
    auto invalid=m;invalid.rules_revision=2;check(!valid_throwdown(invalid),"Unsupported rules revision accepted");
    for(auto value:{skate::NumericalRules{-1,90,450},{181,90,450},{115,-1,450},{115,180,450},{115,90,179},{115,90,1081}}) {
        invalid=m;invalid.numerical=value;check(!valid_throwdown(invalid),"Out-of-range rules accepted");
    }
    for(unsigned cut=1;cut<8;++cut)check(!decode_throwdown(std::span(encoded).first(encoded.size()-cut)),"Truncated rules suffix accepted");
    auto unknown=encoded;unknown[unknown.size()-8]=2;check(!decode_throwdown(unknown),"Unknown received rules revision accepted");
    auto bad=encoded;bad.back()=255;check(!decode_throwdown(bad),"Invalid received recovery limit accepted");
    m.rules_revision=0;const auto legacy=encode_throwdown(m);check(decode_throwdown(legacy)->rules_revision==0,"Legacy message falsely advertises complete rules");
    m.series="JamSession";check(encode_throwdown(m).size()==legacy.size()-(std::string_view("ThrowdownSkate").size()-std::string_view("JamSession").size()),"Non-SKATE encoding changed");
    const auto path=std::filesystem::current_path()/("skate-config-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))/"rules.json";
    skate::HostingPreferences pref;pref.categories=5;pref.numerical={130,60,720};
    {skate::HostPreferences store(path);check(store.set(pref),"Could not persist host preferences");}
    {skate::HostPreferences restarted(path);check(restarted.get()==pref,"Recreated preferences store lost saved values");}
    check(skate::read_hosting_preferences(path)==pref,"Fresh disk read lost host preferences");
    auto corrupt=skate::hosting_json(pref);corrupt["spin_offset"]=180;check(profile_config::write(path,corrupt),"Could not write malformed fixture");
    check(skate::read_hosting_preferences(path)==skate::HostingPreferences{},"Invalid saved preferences did not restore defaults");
    check(profile_config::write(path,skate::hosting_json({})),"Could not reset preferences");
    check(skate::read_hosting_preferences(path)==skate::HostingPreferences{},"Reset did not persist defaults");
    std::filesystem::remove(path);std::filesystem::remove(path.parent_path());
}
