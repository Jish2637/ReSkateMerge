#include "Extension/Skater/skate_host_preferences.h"
#include "Extension/Skater/skate_detector.h"
#include "Extension/Skater/trick_set.h"
#include "Extension/Skater/skate_feedback.h"
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
    // Timing fixtures contain native identity observations with explicit phase
    // bounds. The first grind is exempt; only additional continuous runs qualify.
    const auto make_grind=[](const char* id,unsigned obs,unsigned at){
        Json r{{"observation_id",obs},{"first_seen_ms",at},{"phase_id",1U},{"phase_kind","grind"},{"candidate_segment_id",0U}};
        r["raw_labels"]=Json{{"base_name",id}};return r;
    };
    const auto recording=[](Json rows,unsigned end){
        Json r;r["landed"]=true;r["rotation_complete"]=true;r["spins"]=Json::array();r["ticker_candidates"]=rows;r["movement_phases"]=Json::array({Json{{"id",1U},{"begin",100U},{"end",end},{"segment",0U},{"kind","grind"}}});return r;
    };
    const auto a=make_grind("ID_TRICK_TAILSLIDE",1,100),b=make_grind("ID_TRICK_NOSESLIDE",2,110);
    for(unsigned hold:{499U,500U,501U}) {
        const auto n=skate::normalize(recording(Json::array({a,b}),110+hold),{15,true});
        check(n.signature.error.empty(),"Short additional grind invalidated first grind");
        check(n.ignored.size()==(hold<500?1U:0U),"Additional grind boundary incorrect");
        if(hold<500)check(n.ignored.at(0).at("duration_ms").get<unsigned>()==hold,"Grind duration not measured per identity");
    }
    auto short_only=skate::normalize(recording(Json::array({a}),101),{15,true});
    check(short_only.signature.error.empty() && short_only.ignored.empty(),"First grind requires duration");
    auto duplicate=b;duplicate["first_seen_ms"]=600U;
    auto n=skate::normalize(recording(Json::array({a,b,duplicate}),609),{15,true});
    check(n.ignored.size()==1 && n.ignored.at(0).at("duration_ms").get<unsigned>()==499,"Duplicate observation inflated time");
    auto returned=make_grind("ID_TRICK_TAILSLIDE",3,410);
    n=skate::normalize(recording(Json::array({a,b,returned}),710),{15,true});
    check(n.ignored.size()==2,"Identity re-entry accumulated old hold time");
    auto quick=make_grind("ID_TRICK_50_50",4,510);
    n=skate::normalize(recording(Json::array({a,b,returned,quick}),610),{15,true});
    check(n.ignored.size()==3,"Rail spinning added incidental grind identities");
    n=skate::normalize(recording(Json::array({a,b}),111),{15,true,{115,90,450,0}});
    check(n.ignored.empty(),"Zero duration did not disable timing rule");
    n=skate::normalize(recording(Json::array({a,b}),810),{15,true,{115,90,450,750}});
    check(n.ignored.size()==1,"Custom duration ignored");
    auto later=make_grind("ID_TRICK_NOSESLIDE",5,900);later["phase_id"]=2U;
    auto phases=recording(Json::array({a,later}),200);phases["movement_phases"].push_back(Json{{"id",2U},{"kind","grind"},{"begin",900U},{"end",1399U},{"segment",0U}});
    n=skate::normalize(phases,{15,true});check(n.ignored.size()==1,"Separate phase inherited another rail duration");
    auto ordered=recording(Json::array({a,b}),610),reordered=recording(Json::array({b,a}),610);
    check(skate::normalize(ordered,{15,true}).signature.tokens==skate::normalize(reordered,{15,true}).signature.tokens,"Reordered rows changed timing");
    skate::TrickFeedback feedback;feedback.invalid("Excessive grounded rotation.");
    feedback.ignored(Json::array({Json{{"reason","grind duration"},{"duration_ms",320U},{"minimum_ms",500U}}}));
    feedback.ignored(Json::array({Json{{"reason","grind duration"},{"duration_ms",320U},{"minimum_ms",500U}}}));
    check(feedback.lines(false).size()==1 && feedback.lines(true).size()==2,"Feedback is not optional or deduplicated");
    check(feedback.lines(true).back()=="Grind omitted: 0.32s / 0.50s.","Grind duration feedback unclear");
    const auto signature=skate::normalize(ordered,{15,true}).signature.tokens;
    feedback.lines(false);feedback.lines(true);check(skate::normalize(ordered,{15,true}).signature.tokens==signature,"Feedback changed judging");
    feedback.clear();check(feedback.lines(true).empty(),"Feedback did not clear");
    skate::begin({15,true});skate::turn(true);
    auto bail=trick_logger::SetSample{};bail.valid=true;bail.state=300;skate::sample(bail);
    check(!skate::feedback_lines(false).empty(),"Basic failure feedback absent");
    skate::turn(false);check(skate::feedback_lines(true).empty(),"Turn change kept stale feedback");
    skate::end();check(skate::feedback_lines(true).empty(),"Match end kept feedback");
    check(skate::NumericalRules{115,90,450,0}.valid() && skate::NumericalRules{115,90,450,3000}.valid(),"Grind duration endpoints invalid");
    check(!skate::NumericalRules{115,90,450,-50}.valid() && !skate::NumericalRules{115,90,450,3050}.valid() && !skate::NumericalRules{115,90,450,501}.valid(),"Invalid grind duration accepted");
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
    auto invalid=m;invalid.rules_revision=255;check(!valid_throwdown(invalid),"Unsupported rules revision accepted");
    for(auto value:{skate::NumericalRules{-1,90,450},{181,90,450},{115,-1,450},{115,180,450},{115,90,179},{115,90,1081}}) {
        invalid=m;invalid.numerical=value;check(!valid_throwdown(invalid),"Out-of-range rules accepted");
    }
    for(unsigned cut=1;cut<10;++cut)check(!decode_throwdown(std::span(encoded).first(encoded.size()-cut)),"Truncated rules suffix accepted");
    auto unknown=encoded;unknown[unknown.size()-10]=255;check(!decode_throwdown(unknown),"Unknown received rules revision accepted");
    auto bad=encoded;bad.back()=255;check(!decode_throwdown(bad),"Invalid received grind duration accepted");
    m.rules_revision=0;const auto legacy=encode_throwdown(m);check(decode_throwdown(legacy)->rules_revision==0,"Legacy message falsely advertises complete rules");
    m.series="JamSession";check(encode_throwdown(m).size()==legacy.size()-(std::string_view("ThrowdownSkate").size()-std::string_view("JamSession").size()),"Non-SKATE encoding changed");
    const auto path=std::filesystem::current_path()/("skate-config-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))/"rules.json";
    skate::HostingPreferences pref;pref.categories=5;pref.numerical={130,60,720,750};
    {skate::HostPreferences store(path);check(store.set(pref),"Could not persist host preferences");}
    {skate::HostPreferences restarted(path);check(restarted.get()==pref,"Recreated preferences store lost saved values");}
    check(skate::read_hosting_preferences(path)==pref,"Fresh disk read lost host preferences");
    auto old=skate::hosting_json(pref);old.erase("grind_combo_ms");check(profile_config::write(path,old),"Could not write older preference fixture");
    check(skate::read_hosting_preferences(path).numerical.grind_combo_ms==500,"Older preference file did not default duration");
    auto corrupt=skate::hosting_json(pref);corrupt["spin_offset"]=180;check(profile_config::write(path,corrupt),"Could not write malformed fixture");
    check(skate::read_hosting_preferences(path)==skate::HostingPreferences{},"Invalid saved preferences did not restore defaults");
    check(profile_config::write(path,skate::hosting_json({})),"Could not reset preferences");
    check(skate::read_hosting_preferences(path)==skate::HostingPreferences{},"Reset did not persist defaults");
    const auto feedback_path=path.parent_path()/"feedback.json";
    {skate::FeedbackPreferences f(feedback_path);check(!f.get(),"Detailed feedback default is on");check(f.set(true),"Could not save detailed feedback");}
    {skate::FeedbackPreferences f(feedback_path);check(f.get(),"Detailed feedback did not persist");check(f.set(false),"Could not disable feedback");}
    {skate::FeedbackPreferences f(feedback_path);check(!f.get(),"Disabled feedback did not persist");}
    std::filesystem::remove(feedback_path);
    std::filesystem::remove(path);std::filesystem::remove(path.parent_path());
}
