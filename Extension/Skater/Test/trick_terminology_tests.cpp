#include "Extension/Skater/trick_terminology_rule.h"
#include "Extension/Skater/terminology_spin.h"
#include "Extension/Skater/trick_rules.h"
#include "Extension/Skater/trick_identity.h"
#include "Extension/Skater/terminology_phase_reference.h"
#include "Extension/Skater/completed_ui.h"
#include "Extension/Skater/trick_binding_probe.h"
#include "Extension/Skater/native_combo_rows.h"
#include "Extension/Skater/retained_combos.h"
#include "Extension/Skater/combo_history.h"
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace dingosdk::trick_terminology;
void check(bool ok,const std::source_location location=std::source_location::current()) { if (!ok) {std::cerr<<"Terminology regression at line "<<location.line()<<std::endl;throw std::runtime_error("Terminology regression");} }
int main() {
    {
        TextEdit base, held, direction;
        base.incoming("Pop-Shuvit", "");
        base.applied("Pop-Shuvit", "3 Shuv");
        check(base.observe("")=="Pop-Shuvit"); // read before original writer commits
        base.publication_finished("3 Shuv");
        check(base.observe("3 Shuv")=="Pop-Shuvit");
        held.applied("360", "");direction.applied("BS", "");
        check(held.observe("")=="360" && direction.observe("")=="BS");
        // Repeated capture and restoration use native values, never the alias.
        for(int i=0;i<10;++i)check(base.observe("3 Shuv")=="Pop-Shuvit");
        base.incoming("3 Shuv", "3 Shuv"); // genuine native revision equal to alias
        base.publication_finished("3 Shuv");
        check(!base.active && base.original=="3 Shuv");
        TextEdit failed;failed.incoming("Hardflip", "Ollie");
        failed.applied("Hardflip", "Ghetto Bird");failed.publication_finished("Ollie");
        check(!failed.active && failed.original=="Ollie");
    }

    {
        namespace rows=dingosdk::trick_terminology::combo_rows;
        // Matched action chronology is Ollie(11) -> Tailslide(22).
        for(bool newest:{false,true}) {
            std::vector<std::uint64_t> order=newest?std::vector<std::uint64_t>{22,11}:std::vector<std::uint64_t>{11,22};
            const auto orientation=rows::newest_first(order,{11,22});check(orientation && *orientation==newest);
            const auto before=order;const int native_active=newest?0:1;
            order.insert(order.begin()+rows::insertion_index(order,22,*orientation),33);
            check(order==(newest?std::vector<std::uint64_t>{33,22,11}:std::vector<std::uint64_t>{11,22,33}));
            check(order[rows::remapped_index(before,order,native_active)]==22);
            // The next normal trick follows the custom result in native chronology.
            if(newest)order.insert(order.begin(),44);else order.push_back(44);
            check(order==(newest?std::vector<std::uint64_t>{44,33,22,11}:std::vector<std::uint64_t>{11,22,33,44}));
            const auto old=order;const auto active=static_cast<int>(std::find(order.begin(),order.end(),22)-order.begin());
            order.erase(std::find(order.begin(),order.end(),33));
            check(order[rows::remapped_index(old,order,active)]==22);
            check(order==(newest?std::vector<std::uint64_t>{44,22,11}:std::vector<std::uint64_t>{11,22,44}));
        }
        check(!rows::newest_first({11},{11,22}));
        check(!rows::newest_first({11,33,22},{11,22,33})); // mixed ordering is ambiguous
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        // Any arriving label can complete a rule. Replay all producer orders
        // for base, held rotation, acknowledged spin and Nollie marker.
        ed::Rule rule;rule.combo=true;rule.stance=1;rule.first.base="Hardflip";rule.first.held="360";
        rule.spin=180;rule.direction="FS";rule.name="Custom Flip";
        std::array<unsigned,4> order{0,2,4,6};
        do {
            ed::Observation row;row.handle=1;row.attempt=1;row.valid=true;row.direction="FS";
            unsigned changed{};
            for(auto field:order) {
                row.labels[field]=field==0?"Hardflip":field==2?"180":field==4?"360":"Nollie";
                row.spin_valid=row.labels[2]=="180";++changed;
                const auto result=ed::match({rule},row,{row});
                check(bool(result.rule)==(changed==4));
                if(result.rule) {const auto display=ed::display(rule,row.labels);check(display[0]=="Nollie Custom Flip" && display[2].empty());}
            }
        }while(std::next_permutation(order.begin(),order.end()));
        ed::Observation unknown;unknown.handle=1;unknown.attempt=1;unknown.valid=true;
        unknown.labels={"Hardflip","","180","","360","","Nollie"};unknown.direction="FS";
        check(!ed::match({rule},unknown,{unknown}).rule); // acknowledgement alone is insufficient
    }
    {
        namespace rows=dingosdk::trick_terminology::combo_rows;
        check(std::find(rows::label_fields.begin(),rows::label_fields.end(),"SlamDamageAmount")!=rows::label_fields.end());
        check(std::find(rows::scalar_fields.begin(),rows::scalar_fields.end(),"SlamDamageAmount")==rows::scalar_fields.end());
        check(rows::label_fields.size()==17 && rows::scalar_fields.size()==4);
        check(rows::retry_delay("Combo source is not in its native history stack")==32);
        check(rows::retry_delay("Combo row field differs")==1000);

        check(rows::zeroable_scalar(8,4)); // native ticker enum, previously rejected
        check(!rows::zeroable_scalar(8,8));
        check(rows::zeroable_scalar(10,1));check(!rows::zeroable_scalar(10,4));
        check(rows::zeroable_scalar(15,4));check(rows::zeroable_scalar(19,4));
        check(rows::zeroable_scalar(20,8));check(!rows::zeroable_scalar(19,8));
        for(unsigned kind:{2u,3u,4u,7u,9u,21u,22u})check(!rows::zeroable_scalar(kind,8));
        namespace ed=dingosdk::trick_terminology::editor;
        // Exact saved test2 conditions, with native spin evidence on the Ollie.
        ed::Rule rule;rule.id=80;rule.combo=rule.sequence=true;rule.name="test2";rule.stance_prefix=false;
        rule.first.base="Ollie";rule.first.board="FS";rule.second.base="Tailslide";rule.second.board="FS";
        rule.second.native_id="ID_TRICK_TAILSLIDE";rule.second.next_phase=true;
        ed::Observation a;a.handle=1;a.sequence=a.attempt=9;a.sequence_valid=a.valid=a.spin_valid=true;
        a.phase=1;a.phase_kind="air";a.first_seen=100;a.native_id="ID_TRICK_OLLIE";a.labels={"Ollie","FS","180"};a.direction="FS";
        auto b=a;b.handle=2;b.phase=2;b.phase_kind="grind";b.first_seen=200;b.native_id="ID_TRICK_TAILSLIDE";b.labels={"Tailslide","FS"};b.direction.clear();b.spin_valid=false;
        ed::ComboHistory history;(void)history.merge({a},{9},{});
        auto frame=history.merge({b},{9},{});std::vector<ed::Rule> rules{rule};auto matches=ed::resolve_frame(rules,frame);
        check(matches[1].rule && matches[1].following==std::vector<std::uint64_t>{2});
        auto patches=ed::render_match(rule,matches[1],1,frame);check(patches.at(1)[0]=="test2");
        // A subsequent trick remains an independent row, outside the matched chain.
        auto c=b;c.handle=3;c.phase=3;c.phase_kind="air";c.first_seen=300;c.labels={"Kickflip"};c.native_id="ID_TRICK_KICKFLIP";
        frame=history.merge({b,c},{9},{});matches=ed::resolve_frame(rules,frame);
        check(matches[2].rule);patches=ed::render_match(rule,matches[2],2,frame);check(!patches.contains(3));
        b.sequence=10;check(!ed::match(rules,a,{a,b}).rule);
        b.sequence=9;b.labels[1]="BS";check(!ed::match(rules,a,{a,b}).rule);
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule rule;rule.id=1;rule.combo=true;rule.stance=-1;rule.first.base="Hardflip";rule.spin=180;rule.direction="FS";rule.name="Ghetto Flip";
        ed::Observation row;row.handle=1;row.attempt=1;row.first_seen=1;row.valid=row.spin_valid=true;row.labels[0]="Hardflip";row.labels[2]="180";
        for(int stance=0;stance<4;++stance) {
            row.labels[3]=stance?ed::stance_name(stance):"";row.direction=stance==2?"BS":"FS";
            check(ed::resolve_frame({rule},{row})[0].rule);
            row.direction=stance==2?"FS":"BS";check(!ed::resolve_frame({rule},{row})[0].rule);
        }
        row.labels[3]="Fakie";row.direction="FS";row.labels[1]="FS";row.native_id="ID_TRICK_OLLIE";row.labels[0]="Ollie";
        rule.first.base="Ollie";check(ed::resolve_frame({rule},{row})[0].rule);
        check(ed::direction_test_frame({row})[0].labels[1]=="FS");
        row.labels[0]="Kickflip";row.native_id="ID_TRICK_KICKFLIP";rule.first.base="Kickflip";row.direction="BS";check(ed::resolve_frame({rule},{row})[0].rule);row.direction="FS";
        row.labels[0]="Smith";row.native_id="ID_TRICK_SMITH";rule.first.base="Smith";check(ed::resolve_frame({rule},{row})[0].rule);
        row.labels[0]="Pop-Shuvit";row.native_id="ID_TRICK_POPSHUVIT";rule.first.base="Pop-Shuvit";rule.first.board="FS";row.direction="BS";
        check(ed::resolve_frame({rule},{row})[0].rule);check(ed::direction_test_frame({row})[0].labels[1]=="FS");
        row.direction.clear();check(!ed::resolve_frame({rule},{row})[0].rule);
        auto document=ed::encode_rules({rule});check(document["schema"]==5);
        check(!document["rules"][0].contains("invert_fakie_body") && !document["rules"][0].contains("invert_fakie_board"));
        document["schema"]=4;document["rules"][0]["invert_fakie_body"]=false;document["rules"][0]["invert_fakie_board"]=true;
        auto imported=ed::decode_rules(document);check(!ed::combo_row(imported[0]));
        check(ed::encode(imported[0])["kind"]=="spin_rename");
        auto chain=rule;chain.second.base="Smith";chain.sequence=true;check(ed::combo_row(chain));check(ed::encode(chain)["kind"]=="combo");
        check(!ed::encode_detection({}).contains("native_combo_rows"));
        ed::ComboHistory history;row.sequence=7;row.phase=1;row.sequence_valid=true;row.direction="BS";
        auto retained=history.merge({row},{7},{});check(retained.size()==1);
        auto second=row;second.handle=2;second.first_seen=2;second.phase=2;second.labels[0]="Smith";second.native_id="ID_TRICK_SMITH";second.labels[2].clear();second.spin_valid=false;second.direction.clear();
        retained=history.merge({second},{7},{});check(retained.size()==2);
        chain.second.native_id="ID_TRICK_SMITH";chain.second.board="FS";
        check(ed::resolve_frame({chain},retained)[1].rule);
        retained=history.merge({}, {7}, {7});check(retained.empty());
    }

    {
        namespace ed=dingosdk::trick_terminology::editor;
        // The incoming acknowledged modifier completes a combo virtually;
        // its rendered text is consumed, while a base rename retains it.
        ed::Rule bird;bird.id=1;bird.combo=true;bird.stance=1;bird.first.base="Hardflip";
        bird.spin=180;bird.direction="FS";bird.name="Ghetto Bird";
        std::vector<ed::Rule> rules{bird};ed::Observation row;row.handle=100;row.attempt=9;
        row.valid=true;row.labels[0]="Hardflip";row.labels[3]="Nollie";row.direction="FS";
        check(!ed::resolve_frame(rules,{row})[0].rule);
        row.labels[2]="180";row.spin_valid=true;
        const std::vector<ed::Observation> frame{row};const auto hits=ed::resolve_frame(rules,frame);
        check(hits[0].rule);
        const auto patches=ed::render_match(rules[0],hits[0],0,frame,{row.labels},rules);
        check(patches.at(100)[0]=="Nollie Ghetto Bird" && patches.at(100)[2].empty());
        row.direction="BS";check(!ed::resolve_frame(rules,{row})[0].rule);
        ed::Rule tre;tre.id=2;tre.stance=-1;tre.first.base="Flip";tre.first.held="360";tre.name="Tre Flip";
        row.labels[0]="Flip";row.labels[4]="360";
        const auto renamed=ed::display(tre,row.labels);check(renamed[0]=="Tre Flip" && renamed[2]=="180");
    }

    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule bird;bird.id=77;bird.combo=true;bird.stance=0;bird.first.base="Hardflip";bird.spin=180;bird.direction="BS";bird.name="Ghetto Bird";
        std::vector<ed::Rule> rules{bird};
        ed::Observation row;row.handle=1;row.attempt=1;row.valid=true;row.spin_valid=true;
        row.labels[0]="Hardflip";row.labels[2]="180";row.direction="BS";row.measured_degrees=185;
        std::vector<ed::Observation> frame{row};ed::ConfirmedCombos cache;
        auto matches=ed::resolve_frame(rules,frame);check(matches[0].rule);
        ed::retain_combos(rules,frame,matches,cache);
        // Measurement grows within 180: keep a confirmed result during drift.
        frame[0].measured_degrees=200;frame[0].spin_valid=false;
        matches=ed::resolve_frame(rules,frame);check(!matches[0].rule);
        ed::retain_combos(rules,frame,matches,cache);check(matches[0].rule);
        frame[0].spin_valid=true;
        frame[0].labels[2]="360";frame[0].measured_degrees=365;
        matches=ed::resolve_frame(rules,frame);check(!matches[0].rule);
        ed::retain_combos(rules,frame,matches,cache);check(!matches[0].rule);
        // Higher native spins restore all native fields with no stale alias.
        for(const auto spin:{540,720,900,1080}) {
            frame[0].labels[2]=std::to_string(spin);frame[0].measured_degrees=spin+5;
            matches=ed::resolve_frame(rules,frame);
            ed::retain_combos(rules,frame,matches,cache);check(!matches[0].rule);
        }
        frame[0].labels[2]="360";frame[0].measured_degrees=365;
        auto full=bird;full.id=78;full.spin=360;full.name="Full Spin";rules.push_back(full);
        matches=ed::resolve_frame(rules,frame);ed::retain_combos(rules,frame,matches,cache);
        check(matches[0].rule && matches[0].rule->id==78);
        frame[0].direction="FS";matches=ed::resolve_frame(rules,frame);ed::retain_combos(rules,frame,matches,cache);check(!matches[0].rule);
        frame[0].direction="BS";frame[0].attempt=2;matches=ed::resolve_frame({bird},frame);
        std::vector<ed::Rule> only{bird};ed::retain_combos(only,frame,matches,cache);check(!matches[0].rule);
        auto zebra=bird;zebra.id=90;zebra.name="Zebra";auto alpha=bird;alpha.id=91;alpha.name="alpha";alpha.enabled=false;
        const auto document=ed::encode_rules({zebra,alpha});check(document["rules"][0]["name"]=="alpha");
        auto loaded=ed::decode_rules(document);check(loaded[0].id==91 && loaded[1].id==90);
    }

    // Fabricated VM tests verify pointer-slot indirection and read-only capture.
    {
        namespace bp=binding_probe;
        alignas(16) std::array<std::byte,128> resource{};
        alignas(16) std::array<std::byte,224> instance{};
        std::array<std::uintptr_t,18> frame{};std::array<std::uintptr_t,8> vm{};
        auto put=[](auto& buffer,unsigned at,const auto& value){std::memcpy(buffer.data()+at,&value,sizeof(value));};
        const auto res=reinterpret_cast<std::uintptr_t>(resource.data());
        put(resource,0x10,bp::graph);put(resource,0x20,bp::layout);
        put(instance,0,res);const auto fp=reinterpret_cast<std::uintptr_t>(frame.data());put(instance,200,fp);
        vm[6]=reinterpret_cast<std::uintptr_t>(instance.data());vm[7]=res;
        std::uint64_t context=0x100000001ULL;
        const char* input="ID_TRICK_FLIP";const char* output="Before";const char* over="";const char* localized="Flip";
        frame[0x58/8]=reinterpret_cast<std::uintptr_t>(&context);
        frame[0x38/8]=reinterpret_cast<std::uintptr_t>(&input);
        frame[0x30/8]=reinterpret_cast<std::uintptr_t>(&over);
        frame[0x28/8]=reinterpret_cast<std::uintptr_t>(&output);
        frame[0x40/8]=reinterpret_cast<std::uintptr_t>(&localized);
        const auto vp=reinterpret_cast<std::uintptr_t>(vm.data());
        bp::publish({{context,77,52,0}},GetTickCount64());
        check(!bp::begin(vp,0,false));bp::enabled=true;
        auto* a=bp::begin(vp,0,false);check(a!=nullptr);
        auto* b=bp::begin(vp,12,true);check(b!=nullptr && b!=a);
        output="After";bp::end(b);bp::end(a);
        bp::Record record;check(bp::pop(record));check(record.stable && record.matched && record.row==77 && record.rule==52);
        check(std::string(record.input.bytes.data())=="ID_TRICK_FLIP" && std::string(record.output.bytes.data())=="After");
        check(std::string(input)=="ID_TRICK_FLIP" && std::string(output)=="After");
        while(bp::pop(record)){}
        auto* changed=bp::begin(vp,0,false);check(changed!=nullptr);++context;bp::end(changed);
        check(bp::pop(record) && !record.stable);--context;
        std::uint32_t bad=0;put(resource,0x10,bad);check(!bp::begin(vp,0,false));
        bp::enabled=false;bp::snapshot.store(nullptr);
    }
    std::array<std::string,13> labels{"Hardflip", "BS", "180"};
    check(ghetto_bird(labels));
    for (const auto& rotation : {"", "360", "540"}) { labels[2] = rotation; check(!ghetto_bird(labels)); }
    labels[2] = "180"; labels[1] = "FS"; check(!ghetto_bird(labels));
    labels[1] = "BS"; labels[0] = "Kickflip"; check(!ghetto_bird(labels));
    labels[0] = "Hardflip";
    for (std::size_t i = 3; i < labels.size(); ++i) { labels[i] = "modifier"; check(!ghetto_bird(labels)); labels[i].clear(); }
    check(body_direction("BS", "180") == "BS");
    check(body_direction("", "BS 180") == "BS");
    check(body_direction("FS", "180") == "FS");
    check(body_direction("BS", "FS 180").empty());
    check(body_direction("", "180").empty());
    // Recorded test-1 failure: Hardflip / empty BoardDirection / 180.
    labels[1] = body_direction("", "180"); check(!ghetto_bird(labels));
    labels[1] = body_direction("BS", "180"); check(ghetto_bird(labels));
    labels[1] = body_direction("FS", "180"); check(!ghetto_bird(labels));
    for (bool goofy : {false, true}) {
        for (const auto& stance : {"", "Nollie", "Fakie", "Switch"}) {
            std::array<std::string,13> trick{"Hardflip", "", "180", stance};
            const auto st = stance_of(stance, "");
            const bool want_fs = st == Stance::nollie || st == Stance::fakie;
            bool positive = want_fs;
            if (goofy) positive = !positive;
            if (st == Stance::switch_ || st == Stance::fakie) positive = !positive;
            const auto d = spin_direction(positive ? 180 : -180, goofy, st);
            const auto prefix = std::string(stance).empty() ? "" : std::string(stance)+" ";
            check(hardflip_alias(trick,d) == prefix+"Ghetto Bird");
            check(hardflip_alias(trick,d=="BS"?"FS":"BS").empty());
            trick[5] = "Late"; check(hardflip_alias(trick,d).empty());
        }
    }

    unsigned mappings{};
    for (const auto& stance : {"", "Nollie", "Fakie", "Switch"}) {
        const std::string prefix=std::string(stance).empty()?"":std::string(stance)+" ";
        const auto verify=[&](const char* base,const char* board,const char* rotation,const char* direction,const char* name,bool consumes) {
            std::array<std::string,13> row{base,board,rotation,stance};
            const auto family_direction=std::string(stance)=="Fakie" && consumes ?
                (std::string(direction)=="BS"?"FS":"BS"):direction;
            const auto result=flip_alias(row,family_direction);
            check(result.name==(consumes ? prefix+name : name) && result.consumes_spin==consumes);++mappings;
            for (unsigned slot:{4u,5u,7u,8u,9u,10u,11u,12u}) {
                row[slot]="special";const auto special=flip_alias(row,direction);
                check(consumes ? !special.consumes_spin : special.name=="Tre Flip");row[slot].clear();
            }
        };
        verify("360 Flip","","","","Tre Flip",false);
        verify("360 Pop Shuv-it","BS","180","BS","Bigspin",true);
        verify("360 Pop Shuv-it","FS","180","FS","FS Bigspin",true);
        verify("360 Flip","","180","BS","Bigflip",true);
        verify("360 Hard Flip","","180","FS","FS Bigflip",true);
        verify("360 Inward Heelflip","","180","BS","BS Bigheel",true);
        verify("Laser Flip","","180","FS","Bigheel",true);
        std::array<std::string,13> opposite{"360 Pop Shuv-it","FS","180",stance};
        check(flip_alias(opposite,std::string(stance)=="Fakie"?"FS":"BS").name.empty());
        opposite[0]="Pop Shuv-it";opposite[1]="BS";opposite[4]="540";
        check(flip_alias(opposite,std::string(stance)=="Fakie"?"FS":"BS").name.empty());
    }
    for (const auto& rotation:{"180","360"}) for (const auto& dir:{"BS","FS"})
        for (const auto& base:{"Ollie","Kickflip","Heelflip"}) {
            std::array<std::string,13> row{base,"",rotation,"Fakie"};
            const auto suffix=std::string(base)=="Ollie"?"":std::string(base)=="Kickflip"?" Flip":" Heelflip";
            check(flip_alias(row,dir).name==std::string(std::string(dir)=="FS"?"FS ":"")+
                (std::string(rotation)=="180"?"Half-Cab":"Cab")+suffix);++mappings;
            row[2]="540";check(flip_alias(row,dir).name.empty());
        }

    // Exact native field layouts from the user's failed test4 log.
    for (const auto& stance:{"","Nollie","Fakie","Switch"}) {
        const std::string prefix=std::string(stance).empty()?"":std::string(stance)+" ";
        std::array<std::string,13> row{"Flip","","",stance,"360"};
        auto result=flip_alias(row,"");
        check(result.name=="Tre Flip" && result.consumes_held && !result.consumes_spin);
        row[2]="180";const bool fakie=std::string(stance)=="Fakie";
        const auto bs=fakie?"FS":"BS",fs=fakie?"BS":"FS";result=flip_alias(row,bs);
        check(result.name==prefix+"Bigflip" && result.consumes_held && result.consumes_spin);
        row[0]="Hardflip";check(flip_alias(row,fs).name==prefix+"FS Bigflip");
        row[0]="Inward Heelflip";check(flip_alias(row,bs).name==prefix+"BS Bigheel");
        row[0]="Pop-Shuvit";row[1]="BS";
        check(flip_alias(row,bs).name==prefix+"Bigspin");
        row[1]="FS";check(flip_alias(row,fs).name==prefix+"FS Bigspin");
        row[4]="540";check(flip_alias(row,"FS").name.empty());
        row[4]="360";row[5]="Late";check(flip_alias(row,"FS").name.empty());
    }
    std::array<std::string,13> fakie_ollie{"Ollie","FS","180","Fakie"};
    check(measured_body_direction(fakie_ollie,161,false)=="FS");
    check(flip_alias(fakie_ollie,measured_body_direction(fakie_ollie,161,false)).name=="FS Half-Cab");
    fakie_ollie[0]="Pop-Shuvit";
    check(measured_body_direction(fakie_ollie,161,false)=="BS"); // don't use board direction
    TextEdit held;
    check(held.observe("360")=="360");held.applied("360","");check(held.observe("")=="360");
    check(held.observe("540")=="540" && !held.active);
    check(spin_direction(-155,false,Stance::fakie)=="FS");
    check(spin_direction(155,true,Stance::fakie)=="FS");
    std::array<std::string,13> tre_with_spin{"360 Flip","","360","Switch"};
    check(flip_alias(tre_with_spin,"BS").name=="Tre Flip");
    check(!flip_alias(tre_with_spin,"BS").consumes_spin);
    check(mappings==40); // plus Ghetto Bird cases above

    // A base rename edits exactly the base phrase and its consumed 360 component.
    for (const auto& stance:{"","Nollie","Fakie","Switch"})
        for (const auto& rotation:{"","180","360","540"}) {
            std::array<std::string,13> row{"Flip","FS",rotation,stance,"360","Late","",
                "Backflip","FS","Dark Catch","Dark Flip","","Underflip"};
            const auto alias=flip_alias(row,"");
            check(alias.name=="Tre Flip" && !alias.consumes_spin && alias.consumes_held);
            for (unsigned i=0;i<row.size();++i)
                check(display_field(alias,i,row[i])==(i==0?"Tre Flip":i==4?"":row[i]));
        }
    std::array<std::string,13> fakie_bs_shuv{"Pop-Shuvit","BS","180","Fakie","360"};
    check(flip_alias(fakie_bs_shuv,"FS").name=="Fakie Bigspin");
    check(flip_alias(fakie_bs_shuv,"BS").name.empty());
    fakie_bs_shuv[1]="FS";
    check(flip_alias(fakie_bs_shuv,"BS").name=="Fakie FS Bigspin");
    std::array<std::string,13> tre_fs180{"Flip","","180","","360"};
    const auto base_only=flip_alias(tre_fs180,"FS");
    check(base_only.name=="Tre Flip" && display_field(base_only,2,"180")=="180");
    const auto bigflip_alias=flip_alias(tre_fs180,"BS");
    check(bigflip_alias.name=="Bigflip" && display_field(bigflip_alias,2,"180").empty());
    check(stance_of("Switch","Nollie") == Stance::unknown);
    SpinTracker spins;
    spins.sample(100,1,100,170,true,false);
    spins.sample(116,1,200,-170,true,false);
    for (int i=1; i<=7; ++i) spins.sample(116+i*16,1,201,-170+i*20,true,false);
    spins.sample(244,1,100,-10,true,false);
    auto attempt = spins.associate(150);
    check(attempt && attempt->landed && attempt->valid && std::abs(attempt->net-180)<.01);
    check(!spins.associate(1100));
    spins.sample(260,1,200,10,true,false);
    check(!spins.associate(260)); // delayed row overlaps two attempts
    spins.sample(500,1,201,30,true,false);
    spins.sample(516,1,100,50,true,false);
    check(!spins.attempts.back()->valid); // frame gap
    spins.sample(532,1,200,70,true,false);
    spins.sample(548,1,300,90,true,false);
    check(!spins.attempts.back()->valid); // bail
    spins.sample(564,2,100,0,true,false);
    check(spins.attempts.empty()); // entity replacement
    SpinTracker landing;
    landing.sample(100,1,100,0,true,false);
    landing.sample(116,1,200,20,true,false);
    landing.sample(132,1,201,40,true,false);
    landing.sample(148,1,101,60,true,false);
    landing.sample(164,1,101,80,true,false);
    check(!landing.attempts.back()->end);
    landing.sample(180,1,100,100,true,false);
    check(landing.attempts.back()->landed && landing.attempts.back()->net == 100);
    check(landing.attempts.back()->air_net == 60); // later grounded rotation is separate
    TextEdit name, direction;
    check(name.observe("Hardflip") == "Hardflip"); name.applied("Hardflip", "Ghetto Bird");
    direction.applied("BS", "");
    check(name.observe("Ghetto Bird") == "Hardflip");
    check(direction.observe("") == "BS");
    // A native name revision wins, while the unchanged direction still restores.
    check(name.observe("Kickflip") == "Kickflip"); check(!name.active);
    check(direction.observe("") == "BS");
    check(name.observe("Ghetto Bird") == "Ghetto Bird");
    {
        namespace ed=dingosdk::trick_terminology::editor;
        const std::vector<ed::Rule> empty;
        ed::Observation row;row.handle=1;row.attempt=7;row.first_seen=100;row.valid=true;
        row.labels={"Flip","","180","Fakie","360"};row.direction="BS";row.spin_valid=true;
        check(!ed::match(empty,row,{row}).rule); // genuinely clean slate, no hardcoded fallback
        const auto preset=ed::current_preset();check(preset.size()==44);
        const auto loaded=ed::decode_rules(ed::encode_rules(preset));check(loaded.size()==44);
        check(ed::encode_rules(loaded).dump()==ed::encode_rules(preset).dump());
        auto equivalent=preset[1];equivalent.first.base="Hard Flip";
        check(ed::same_conditions(equivalent,preset[1]));
        auto rename_with_modifier=preset[0];rename_with_modifier.first.modifiers[0]="Late";
        check(ed::same_conditions(rename_with_modifier,preset[0]));
        for (const auto& rule:preset) {
            ed::validate(rule);
            ed::Observation sample;sample.handle=1;sample.attempt=1;sample.first_seen=1;sample.valid=true;sample.spin_valid=true;
            sample.labels[0]=rule.first.base;sample.labels[1]=rule.first.board;sample.labels[4]=rule.first.held;
            sample.labels[3]=rule.stance?ed::stance_name(rule.stance):"";
            sample.labels[2]=rule.spin?std::to_string(rule.spin):"";sample.direction=rule.direction=="Either"?"":rule.direction;
            if(ed::stance_index(sample.labels)==2 && ed::fakie_flip(sample.labels,sample.native_id))sample.direction=ed::opposite_direction(sample.direction);
            const auto hit=ed::match(preset,sample,{sample});check(hit.rule && hit.rule->id==rule.id);
            auto off=rule;off.enabled=false;check(!ed::match({off},sample,{sample}).rule);
        }
        auto rename=preset.front();rename.stance=2;rename.name="Tre Flip";
        row.labels[5]="Late";row.labels[9]="Dark Catch";row.labels[12]="Underflip";
        const auto renamed=ed::display(rename,row.labels);
        for (unsigned i=0;i<row.labels.size();++i) check(renamed[i]==(i==0?"Tre Flip":i==4?"":row.labels[i]));
        check(ed::match({rename},row,{row}).rule);
        auto combo=preset[20]; // Fakie Bigflip, exact selected action and spin
        row.labels[5].clear();row.labels[9].clear();row.labels[12].clear();
        check(combo.name=="Fakie Bigflip" && ed::match({rename,combo},row,{row}).rule->id==combo.id);
        auto ambiguous=combo;ambiguous.id=500;ambiguous.name="Another name";
        check(ed::match({combo,ambiguous},row,{row}).conflict);
        auto broad=combo;broad.id=501;broad.direction="Either";
        check(ed::match({broad,combo},row,{row}).rule->id==combo.id);
        row.direction.clear();check(ed::match({broad},row,{row}).rule);
        row.spin_valid=false;check(!ed::match({broad},row,{row}).rule);row.spin_valid=true;row.direction="BS";
        bool rejected{};
        try {auto document=ed::encode_rules({combo,ambiguous});(void)ed::decode_rules(document);} catch (...) {rejected=true;}
        check(rejected);
        row.labels[3]="Switch";check(!ed::match({combo},row,{row}).rule);row.labels[3]="Fakie";
        row.labels[2]="360";check(!ed::match({combo},row,{row}).rule);row.labels[2]="180";
        row.direction="FS";check(!ed::match({combo},row,{row}).rule);row.direction="BS";
        row.valid=false;check(!ed::match({combo},row,{row}).rule);row.valid=true;
        ed::Rule pair;pair.id=100;pair.combo=true;pair.stance=2;pair.name="Test combination";
        pair.first={"Flip","360","",{}};pair.second={"Kickflip","","",{}};ed::validate(pair);
        ed::Observation second=row;second.handle=2;second.first_seen=120;second.labels[0]="Kickflip";second.labels[4].clear();
        auto hits=ed::resolve_frame({rename,pair},{row,second});check(hits[0].rule && hits[0].second==2 && !hits[1].rule);
        check(ed::display(pair,second.labels,true)[0].empty());
        check(ed::display(pair,second.labels,true)[2]=="180"); // no spin requirement: don't consume one
        second.attempt=8;check(!ed::match({pair},row,{row,second}).rule);second.attempt=7;
        second.first_seen=90;check(!ed::match({pair},row,{row,second}).rule);second.first_seen=120;
        second.first_seen=100;check(!ed::match({pair},row,{row,second}).rule);second.first_seen=120;
        auto third=second;third.handle=3;check(!ed::match({pair},row,{row,second,third}).rule);
        check(ed::unhex(ed::hex(ed::encode(combo).dump()))==ed::encode(combo).dump());
        rejected=false;try {(void)ed::unhex("xx");} catch (...) {rejected=true;}check(rejected);
        rejected=false;try {auto invalid=rename;invalid.name=" ";ed::validate(invalid);} catch (...) {rejected=true;}check(rejected);
        ed::Rule cab=preset[32];auto native=row.labels;native[0]="Ollie";native[1]="ID_TRICK_BS";native[13]="ID_TRICK_BS";
        const auto cab_output=ed::display(cab,native);check(cab_output[1].empty() && cab_output[2].empty() && cab_output[13].empty());
    }

    {
        namespace ed=dingosdk::trick_terminology::editor;
        auto any=ed::current_preset().front();any.stance=-1;any.id=1000;
        ed::Observation row;row.handle=1;row.labels={"Flip","","180","","360"};
        for (int st=0;st<4;++st) {
            row.labels[3]=st?ed::stance_name(st):"";
            check(ed::match({any},row,{row}).rule);
            const auto shown=ed::display(any,row.labels);
            check(shown[3]==row.labels[3] && shown[2]=="180");
        }
        row.labels[3]="Switch";row.labels[6]="Nollie";
        check(!ed::match({any},row,{row}).rule);row.labels[6].clear();
        auto subset=any;subset.id=1001;subset.stance=-2;subset.stance_mask=9;
        for (int st=0;st<4;++st) {
            row.labels[3]=st?ed::stance_name(st):"";
            check((ed::match({subset},row,{row}).rule!=nullptr)==(st==0 || st==3));
        }
        row.labels[3].clear();auto specific=any;specific.id=1002;specific.stance=0;
        check(ed::match({any,subset,specific},row,{row}).rule->id==specific.id);
        check(ed::match({any,subset},row,{row}).rule->id==subset.id);
        auto overlap=subset;overlap.id=1003;overlap.stance_mask=3;
        check(ed::conflicting_conditions(subset,overlap));
        check(ed::match({subset,overlap},row,{row}).conflict);
        auto disjoint=subset;disjoint.id=1004;disjoint.stance_mask=6;
        check(!ed::conflicting_conditions(subset,disjoint));
        auto all_multi=any;all_multi.stance=-2;all_multi.stance_mask=15;
        check(ed::same_conditions(any,all_multi));
        auto one_multi=specific;one_multi.stance=-2;one_multi.stance_mask=1;
        check(ed::same_conditions(specific,one_multi));
        const auto saved=ed::encode_rules({any,subset,specific});
        check(saved.at("schema").get<int>()==5);
        const auto round_trip=ed::decode_rules(saved);
        check(ed::encode_rules(round_trip).dump()==saved.dump());
        auto legacy=ed::encode_rules(ed::current_preset());legacy["schema"]=1;
        for (auto& rule:legacy["rules"]) rule.erase("stance_mask");
        const auto old_rules=ed::decode_rules(legacy);check(old_rules.size()==44);
        for (unsigned i=0;i<old_rules.size();++i)
            check(ed::takeoff_mask(old_rules[i])==(1u<<static_cast<unsigned>(old_rules[i].stance)));
        bool rejected{};
        try {(void)ed::decode_rules(ed::encode_rules({subset,overlap}));} catch (...) {rejected=true;}check(rejected);
        rejected=false;try {auto bad=subset;bad.stance_mask=0;ed::validate(bad);} catch (...) {rejected=true;}check(rejected);
        rejected=false;try {auto bad=subset;bad.stance_mask=16;ed::validate(bad);} catch (...) {rejected=true;}check(rejected);
        rejected=false;try {auto bad=ed::encode(any);bad.erase("stance");(void)ed::decode_rule(bad);} catch (...) {rejected=true;}check(rejected);
        auto captured_scope=subset;captured_scope.first={"Hardflip","","",{}};
        check(ed::takeoff_mask(captured_scope)==9); // capture changes components, not custom stance selection
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::DetectionSettings settings{true,15};
        check(ed::early_spin_degrees(164,3,true,true,settings)==0);
        check(ed::early_spin_degrees(165,3,true,true,settings)==180);
        check(ed::early_spin_degrees(-165,-3,true,true,settings)==180);
        check(ed::early_spin_degrees(345,3,true,true,settings)==360);
        check(ed::early_spin_degrees(525,3,true,true,settings)==540);
        check(ed::early_spin_degrees(705,3,true,true,settings)==720);
        check(!ed::early_spin_degrees(165,-3,true,true,settings));
        check(!ed::early_spin_degrees(165,3,false,true,settings));
        check(!ed::early_spin_degrees(165,3,true,false,settings));
        settings.early_spin=false;check(!ed::early_spin_degrees(180,3,true,true,settings));
        settings={true,0};check(!ed::early_spin_degrees(179,3,true,true,settings));
        check(ed::early_spin_degrees(180,3,true,true,settings)==180);
        auto document=ed::encode_rules(ed::current_preset());document["detection"]=ed::encode_detection({true,25});
        check(ed::decode_detection(document).early_spin && ed::decode_detection(document).lead_degrees==25);
        document.erase("detection");check(!ed::decode_detection(document).early_spin && ed::decode_detection(document).lead_degrees==15);
        document["detection"]=ed::encode_detection({true,46});bool rejected{};
        try {(void)ed::decode_detection(document);}catch(...){rejected=true;}check(rejected);
        ed::Rule rule;rule.id=1;rule.combo=true;rule.first.base="Hardflip";rule.name="Ghetto Bird";rule.spin=180;rule.direction="BS";
        ed::Observation row;row.handle=1;row.attempt=1;row.valid=true;row.labels[0]="Hardflip";row.direction="BS";row.early_degrees=180;
        check(!ed::match({rule},row,{row}).rule);row.labels[2]="180";
        check(ed::match({rule},row,{row}).rule);
        row.direction="FS";check(!ed::match({rule},row,{row}).rule);row.direction="BS";
        row.early_degrees=360;check(!ed::match({rule},row,{row}).rule);
        row.labels[2]="360";row.early_degrees=180;check(!ed::match({rule},row,{row}).rule);
        row.early_degrees=0;row.labels[2]="180";row.spin_valid=true;check(ed::match({rule},row,{row}).rule);
        rule.first.native_id="ID_TRICK_HARDFLIP";row.native_id=rule.first.native_id;row.labels[0]="Localized Hardflip";
        check(ed::match({rule},row,{row}).rule);row.native_id="ID_TRICK_HEELFLIP";check(!ed::match({rule},row,{row}).rule);
        row.native_id.clear();row.labels[0]="Hardflip";check(!ed::match({rule},row,{row}).rule);
        row.native_id=rule.first.native_id;row.identity_ambiguous=true;check(!ed::match({rule},row,{row}).rule);row.identity_ambiguous=false;
        for (int st=0;st<4;++st) for (bool goofy:{false,true}) {
            row.labels[3]=st?ed::stance_name(st):"";row.labels[2]="180";row.early_degrees=180;
            rule.stance=st;const auto stance=stance_of(row.labels[3],row.labels[6]);
            row.direction=spin_direction(-165,goofy,stance);rule.direction=st==2?ed::opposite_direction(row.direction):row.direction;
            check(ed::match({rule},row,{row}).rule);
        }
        rule.stance=0;row.labels[3].clear();rule.direction="BS";row.direction="BS";
        const auto saved=ed::decode_rule(ed::encode(rule));check(saved.first.native_id==rule.first.native_id);
        dingosdk::trick_names::NativeTrickCatalog catalog;catalog.add("ID_TRICK_HARDFLIP","Localized Hardflip");
        check(catalog.resolve("Localized Hardflip",true)=="ID_TRICK_HARDFLIP");
        catalog.add("ID_TRICK_HEELFLIP","Localized Hardflip");check(catalog.resolve("Localized Hardflip",true).empty());check(catalog.ambiguous("Localized Hardflip",true));
        ed::Rule combo=rule;combo.spin=0;combo.direction="Either";combo.first.native_id.clear();combo.first.base="Hardflip";combo.second.base="Kickflip";
        row.labels[0]="Hardflip";row.native_id.clear();row.phase=1;row.first_seen=10;
        auto second=row;second.handle=2;second.labels[0]="Kickflip";second.first_seen=20;second.phase=2;
        check(!ed::match({combo},row,{row,second}).rule);second.phase=1;check(ed::match({combo},row,{row,second}).rule);
        dingosdk::trick_logger::TrickSetTracker phases{false};
        dingosdk::trick_logger::SetSample sample;sample.valid=true;sample.entity=1;sample.now=100;sample.steps=100;sample.state=100;
        (void)phases.update(sample);sample.now=116;sample.steps=116;sample.state=200;(void)phases.update(sample);
        sample.now=132;sample.state=400;(void)phases.update(sample);
        sample.now=148;sample.state=200;(void)phases.update(sample);
        check(phases.phases().size()==3 && phases.phases()[0].kind=="air" && phases.phases()[1].kind=="grind" && phases.phases()[2].segment==2);
        sample.now=500;check(phases.update(sample).has_value()); // interrupted samples close the set
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule rule;rule.id=1;rule.combo=true;rule.stance=-1;rule.name="Custom sequence";rule.first.base="Kickflip";rule.second.base="Smith";
        rule.sequence=true;rule.second.next_phase=true;
        ed::Component exit;exit.base="Heelflip";exit.body_spin=180;exit.body_direction="FS";exit.next_phase=true;rule.extra={exit};
        ed::Observation a;a.handle=1;a.first_seen=10;a.labels[0]="Kickflip";a.sequence=1;a.sequence_valid=true;a.phase=1;
        auto b=a;b.handle=2;b.first_seen=20;b.labels[0]="Smith";b.phase=2;
        auto c=a;c.handle=3;c.first_seen=30;c.labels[0]="Heelflip";c.phase=3;c.labels[2]="180";c.direction="FS";c.spin_valid=true;
        auto match=ed::match({rule},a,{a,b,c});check(match.rule && match.following.size()==2 && match.second==2);
        c.direction="BS";check(!ed::match({rule},a,{a,b,c}).rule);c.direction="FS";
        c.sequence=2;check(!ed::match({rule},a,{a,b,c}).rule);c.sequence=1;
        b.phase=1;check(!ed::match({rule},a,{a,b,c}).rule);b.phase=2;
        b.sequence_valid=false;check(!ed::match({rule},a,{a,b,c}).rule);b.sequence_valid=true;
        auto duplicate=b;duplicate.handle=4;duplicate.first_seen=21;check(!ed::match({rule},a,{a,b,duplicate,c}).rule);
        a.completed=true;a.completed_chain=false;check(!ed::match({rule},a,{a,b,c}).rule);a.completed=false;
        auto saved=ed::encode_rules({rule});const auto loaded=ed::decode_rules(saved);check(ed::encode_rules(loaded).dump()==saved.dump());
        ed::validate(rule);auto too_many=rule;too_many.extra.resize(5);bool rejected{};try{ed::validate(too_many);}catch(...){rejected=true;}check(rejected);
        auto six=rule;six.second.next_phase=false;six.extra.clear();six.sequence=false;six.first.base="Kickflip";six.second.base="Heelflip";
        for(const auto* base:{"Hardflip","Pop-Shuvit","Smith","Tailslide"}) {ed::Component part;part.base=base;six.extra.push_back(part);}
        std::vector<ed::Observation> rows;const auto parts=ed::following(six);auto first=a;first.valid=true;first.attempt=1;rows.push_back(first);
        for(unsigned i=0;i<parts.size();++i){auto row=first;row.handle=i+2;row.first_seen=20+i;row.labels[0]=parts[i].base;rows.push_back(row);}
        check(ed::match({six},rows.front(),rows).following.size()==5);
        rows.pop_back();check(!ed::match({six},rows.front(),rows).rule);
        ed::Rule rename;rename.id=10;rename.stance=-1;rename.first={"Flip","360","",{}};rename.name="Tre Flip";
        check(ed::replace_completed_text("360 Flip",{rename})=="Tre Flip");
        check(ed::replace_completed_text("Switch BS 180 360 Flip",{rename})=="Switch BS 180 Tre Flip");
        check(ed::replace_completed_text("A player named 360 Flip wins",{rename})=="A player named 360 Flip wins");
        check(ed::replace_completed_text("360 Flip",{})=="360 Flip");
        check(ed::replace_completed_text("360 Flip + Unknown Grab",{rename})=="Tre Flip + Unknown Grab");
        ed::Rule shuv_rename=rename;shuv_rename.first={"Pop-Shuvit","","",{}};shuv_rename.name="Shuv";
        check(ed::replace_completed_text("BS 180 Pop-Shuvit",{shuv_rename})=="BS 180 Shuv");
        ed::Rule bird;bird.id=11;bird.stance=0;bird.combo=true;bird.first.base="Hardflip";bird.spin=180;bird.direction="BS";bird.name="Ghetto Bird";
        check(ed::replace_completed_text("BS 180 Hardflip",{bird})=="Ghetto Bird");
        check(ed::replace_completed_text("Hardflip BS 180",{bird})=="Ghetto Bird");
        check(ed::replace_completed_text("FS 180 Hardflip",{bird})=="FS 180 Hardflip");
        check(ed::replace_completed_text("180 Hardflip",{bird})=="180 Hardflip");
        ed::Rule combo;combo.id=12;combo.stance=-1;combo.combo=true;combo.first.base="Kickflip";combo.second.base="Heelflip";combo.name="Double action";
        check(ed::replace_completed_text("Kickflip + Heelflip",{combo})=="Double action");
        check(ed::replace_completed_text("Heelflip + Kickflip",{combo})=="Heelflip + Kickflip");
        ed::Rule chain;chain.id=13;chain.stance=-1;chain.combo=chain.sequence=true;chain.first.base="Kickflip";chain.second.base="Smith";chain.second.next_phase=true;chain.extra.push_back(ed::Component{"Heelflip","","",{}});chain.extra[0].next_phase=true;chain.name="Grind combination";
        check(ed::replace_completed_text("Kickflip | Smith | Heelflip",{chain})=="Grind combination");
        auto legacy=ed::encode_rules(ed::current_preset());legacy["schema"]=2;
        for(auto& r:legacy["rules"]){r.erase("extra");r.erase("sequence");}
        check(ed::decode_rules(legacy).size()==44);
        std::vector<ed::Observation> crowded(100,first);unsigned ordinal{};
        for(auto& row:crowded){row.handle=++ordinal;row.first_seen=ordinal;row.labels[0]="Kickflip";}
        auto expensive=six;expensive.first.base=expensive.second.base="Kickflip";for(auto& part:expensive.extra)part.base="Kickflip";expensive.extra.back().base="Absent";
        check(!ed::match({expensive},crowded.front(),crowded).rule);
        auto render=std::make_shared<ed::View>();render->rules={rename};render->revision=1;render->terminology_enabled=false;ed::feed().store(render);
        check(ed::local_completed_name("360 Flip")=="360 Flip");
        render=std::make_shared<ed::View>(*render);render->terminology_enabled=true;ed::feed().store(render);check(ed::local_completed_name("360 Flip")=="Tre Flip");
        render=std::make_shared<ed::View>(*render);render->revision=2;render->rules[0].name="Changed";ed::feed().store(render);check(ed::local_completed_name("360 Flip")=="Changed");
    }
    {
        namespace logger=dingosdk::trick_logger;
        namespace ed=dingosdk::trick_terminology::editor;
        logger::SpinSegment segment;
        segment.air=311.5;check(logger::measured_spin(segment)==360);
        segment.air=290;segment.landing=70;check(logger::measured_spin(segment)==360);
        segment.air=300;segment.landing=-120;check(logger::measured_spin(segment)==180);
        segment.air=269.9;segment.landing=0;check(logger::measured_spin(segment)==180);
        segment.air=270;check(logger::measured_spin(segment)==360);
        segment.air=450;check(logger::measured_spin(segment)==540);
        segment.air=-300;segment.landing=-60;check(logger::measured_spin(segment)==360);
        segment.grind_adjacent=true;segment.air=114;segment.landing=0;check(!logger::measured_spin(segment));
        segment.air=115;check(logger::measured_spin(segment)==180);
        logger::TrickSetTracker tracker{true};logger::SetSample sample;
        sample.valid=true;sample.entity=1;sample.goofy=false;sample.now=100;sample.steps=1;sample.state=100;
        const auto update=[&](unsigned state,double yaw,std::uint64_t ends=0) {
            sample.state=state;sample.yaw=yaw;sample.now+=16;++sample.steps;sample.sequence_ends=ends;return tracker.update(sample);
        };
        (void)tracker.update(sample);(void)update(200,0);(void)update(201,140);(void)update(201,280);
        (void)update(101,300);(void)update(101,360);(void)update(103,380);
        check(tracker.active_id()!=0); // manual/ground state alone cannot finalize before native sequence end
        (void)update(103,460); // ordinary manual turning must not add 80 degrees
        check(tracker.evidence().segments[0].air==300 && tracker.evidence().segments[0].landing==80);
        const auto finished=update(103,470,1);
        check(finished && finished->landed==true && finished->rotation_complete);
        check(finished->segments[0].accepted_net==380 && finished->segments[0].degrees==360);
        check(!tracker.active_id());
        ed::Observation row;row.valid=true;row.attempt=1;row.handle=1;row.labels[0]="Ollie";row.direction="BS";
        ed::Rule rule;rule.id=1;rule.combo=true;rule.first.base="Ollie";rule.spin=360;rule.direction="BS";rule.name="Custom 360";
        row.spin_valid=true;row.early_degrees=360;
        check(!ed::match({rule},row,{row}).rule); // physical/predicted 360 alone cannot grant a name
        row.labels[2]="180";check(ed::match({rule},row,{row}).rule); // acknowledged spin permits provisional next increment
        row.early_degrees=0;check(!ed::match({rule},row,{row}).rule);
        row.labels[2]="360";check(ed::match({rule},row,{row}).rule);
        row.spin_valid=false;check(!ed::match({rule},row,{row}).rule); // exact native value still needs measured agreement
        logger::TrickSetTracker reset{true};logger::SetSample teleport;teleport.valid=true;teleport.entity=1;teleport.position_known=true;teleport.now=100;teleport.state=100;
        (void)reset.update(teleport);teleport.now=116;teleport.state=200;(void)reset.update(teleport);
        teleport.now=132;teleport.position[0]=6;const auto reset_result=reset.update(teleport);
        check(reset_result && reset_result->outcome=="position_reset" && !reset_result->landed.has_value());
        logger::TrickSetTracker missing{true};sample={};sample.entity=1;sample.valid=true;sample.state=100;sample.now=100;sample.steps=1;(void)missing.update(sample);
        sample.state=200;sample.now=116;++sample.steps;(void)missing.update(sample);
        sample.now=132;sample.valid=false;(void)missing.update(sample);
        sample.now=148;sample.valid=true;sample.yaw=180;(void)missing.update(sample);check(!missing.evidence().rotation_complete);
        sample.state=300;sample.now=164;const auto bailed=missing.update(sample);check(bailed && bailed->landed==false);
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule rule;rule.id=1;rule.combo=true;rule.stance=-2;rule.stance_mask=3;rule.first={"Pop-Shuvit","","FS",{}};
        rule.spin=180;rule.direction="BS";rule.name="Sex Change";rule.stance_prefix=true;
        ed::Labels native{"Pop-Shuvit","FS","180"};
        check(ed::display(rule,native)[0]=="Sex Change");
        native[3]="Nollie";auto shown=ed::display(rule,native);check(shown[0]=="Nollie Sex Change" && shown[3].empty() && shown[6].empty());
        native[3]="ID_STANCE_NOLLIE";check(ed::display(rule,native)[0]=="Nollie Sex Change");
        native[3]="ID_STANCE_FAKIE";check(ed::display(rule,native)[0]=="Fakie Sex Change");
        native[3]="ID_STANCE_SWITCH";check(ed::display(rule,native)[0]=="Switch Sex Change");
        native[3].clear();native[6]="ID_TRICK_NOLLIE";check(ed::display(rule,native)[0]=="Nollie Sex Change");
        check(ed::display(rule,native,true)[0].empty()); // consumed secondary rows must not acquire a prefix
        rule.stance_prefix=false;check(ed::display(rule,native)[0]=="Sex Change");
        rule.stance_prefix=true;rule.name="Nollie Sex Change";check(ed::display(rule,native)[0]=="Nollie Sex Change");
        rule.name="Sex Change";auto decoded=ed::decode_rule(ed::encode(rule));check(decoded.stance_prefix);
        auto legacy=ed::encode(rule);legacy.erase("stance_prefix");check(!ed::decode_rule(legacy).stance_prefix);
        ed::Rule rename;rename.id=2;rename.stance=-1;rename.first={"Flip","360","",{}};rename.name="Tre Flip";
        native={"Flip","BS","180","Nollie","360"};shown=ed::display(rename,native);
        check(shown[3]=="Nollie" && shown[2]=="180" && shown[0]=="Tre Flip");
        rename.stance_prefix=false;shown=ed::display(rename,native);check(shown[3].empty() && shown[2]=="180");
        check(ed::replace_completed_text("Nollie BS 180 360 Flip",{rename})=="BS 180 Tre Flip");
        rename.first={"Pop-Shuvit","","",{}};rename.name="Shuv";
        check(ed::replace_completed_text("Nollie BS 180 Pop-Shuvit",{rename})=="BS 180 Shuv");
        rename.stance_prefix=true;rename.name="Nollie Shuv";
        check(ed::replace_completed_text("Nollie BS 180 Pop-Shuvit",{rename})=="BS 180 Nollie Shuv");
        legacy=ed::encode(rename);legacy.erase("stance_prefix");check(ed::decode_rule(legacy).stance_prefix);
        ed::Rule bird;bird.id=3;bird.stance=-1;bird.combo=true;bird.first.base="Hardflip";bird.spin=180;bird.direction="BS";bird.name="Ghetto Bird";
        check(ed::replace_completed_text("Nollie BS 180 Hardflip",{bird})=="Nollie Ghetto Bird");
        bird.stance_prefix=false;check(ed::replace_completed_text("Nollie BS 180 Hardflip",{bird})=="Ghetto Bird");
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule rule;rule.id=100;rule.combo=rule.sequence=true;rule.stance=-1;rule.name="Custom Grind";
        rule.first.base="*";rule.spin=180;rule.direction="BS";
        rule.second={"Smith","","FS",{}};rule.second.next_phase=true;rule.second.native_id="ID_TRICK_SMITH";
        ed::Component exit;exit.base="*";exit.body_spin=180;exit.body_direction="FS";exit.next_phase=true;rule.extra={exit};
        ed::validate(rule);
        const auto preview=ed::describe(rule);
        check(preview.find("BS 180 body spin")<preview.find("Smith"));
        check(preview.find("Smith")<preview.find("FS 180 body spin"));
        auto fs_first=rule;fs_first.direction="FS";fs_first.extra.clear();
        check(ed::describe(fs_first).find("FS 180 body spin")<ed::describe(fs_first).find("Smith"));
        ed::Observation a;a.handle=1;a.sequence=a.attempt=1;a.sequence_valid=a.valid=true;a.phase=1;a.segment=1;a.phase_kind="air";a.first_seen=10;
        a.labels={"Kickflip","","180","Nollie"};a.native_id="ID_TRICK_KICKFLIP";a.direction="BS";a.spin_valid=true;
        auto b=a;b.handle=2;b.first_seen=20;b.phase=2;b.phase_kind="grind";b.labels={"Smith","FS","","Switch"};b.native_id="ID_TRICK_SMITH";b.spin_valid=false;
        auto c=a;c.handle=3;c.first_seen=30;c.phase=3;c.segment=2;c.labels={"Hardflip","","180","Fakie"};c.native_id="ID_TRICK_HARDFLIP";c.direction="BS";
        std::vector<ed::Observation> frame{a,b,c};auto match=ed::match({rule},a,frame);check(match.rule && match.following.size()==2);
        // Reversing the entry spin and grind phases cannot satisfy Action 1 -> Action 2.
        auto reversed=frame;reversed[0].phase=2;reversed[0].first_seen=20;reversed[1].phase=1;reversed[1].first_seen=10;
        check(!ed::match({rule},reversed[0],reversed).rule);
        auto patches=ed::render_match(rule,match,0,frame);
        check(patches[1][0]=="Kickflip" && patches[1][2].empty());
        check(patches[2][0]=="Nollie Custom Grind" && patches[2][3].empty());
        check(patches[3][0]=="Hardflip" && patches[3][2].empty());
        for(const auto* flip:{"Heelflip","Pop-Shuvit","360 Flip","Impossible"}) {
            frame[0].labels[0]=flip;check(ed::match({rule},frame[0],frame).rule);
        }
        frame={a,b,c};frame[2].direction="FS";check(!ed::match({rule},a,frame).rule);frame={a,b,c};
        frame[0].labels[2].clear();check(!ed::match({rule},frame[0],frame).rule);frame={a,b,c};
        frame[2].phase_kind="grind";check(!ed::match({rule},a,frame).rule);frame={a,b,c};
        auto duplicate=a;duplicate.handle=4;duplicate.first_seen=11;duplicate.labels[0]="Ollie";frame.push_back(duplicate);
        auto specific=rule;specific.id=102;specific.first.base="Kickflip";
        const auto specific_match=ed::match({rule,specific},a,frame);check(specific_match.rule && specific_match.rule->id==specific.id);
        const auto resolved=ed::resolve_frame({rule},frame);check(resolved[0].rule && !resolved[3].rule);
        patches=ed::render_match(rule,resolved[0],0,frame);check(patches[4][0]=="Ollie" && patches[4][2].empty());
        ed::Rule rename;rename.id=101;rename.stance=-1;rename.first.base="Kickflip";rename.name="Custom Flip";
        patches=ed::render_match(rule,resolved[0],0,frame,{}, {rule,rename});check(patches[1][0]=="Custom Flip");
        auto bad=rule;bad.sequence=false;bool rejected{};try{ed::validate(bad);}catch(...){rejected=true;}check(rejected);
        bad=rule;bad.extra[0].body_spin=0;bad.extra[0].body_direction="Either";rejected=false;try{ed::validate(bad);}catch(...){rejected=true;}check(rejected);
        check(ed::encode_rules(ed::decode_rules(ed::encode_rules({rule}))).dump()==ed::encode_rules({rule}).dump());
        ed::Component kick; kick.base="Kickflip";kick.native_id="ID_TRICK_KICKFLIP";
        ed::Component heel; heel.base="Heelflip";heel.native_id="ID_TRICK_HEELFLIP";
        const std::vector<ed::Component> dictionary{kick,heel};
        check(ed::replace_completed_text("Nollie BS 180 Kickflip | FS Smith | FS 180 Heelflip",{rule},dictionary)=="Nollie Kickflip | Nollie Custom Grind | Heelflip");
        check(ed::replace_completed_text("Nollie BS 180 Kickflip | FS Smith | BS 180 Heelflip",{rule},dictionary)=="Nollie BS 180 Kickflip | FS Smith | BS 180 Heelflip");
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        ed::Rule tre;tre.id=1;tre.stance=-1;tre.first={"Flip","360","",{}};tre.name="Tre Flip";tre.rotation_multiples=true;
        ed::validate(tre);ed::Observation row;row.handle=1;row.labels[0]="Flip";
        for(unsigned factor=1;factor<=12;++factor) {
            row.labels[4]=std::to_string(360*factor);
            check(ed::match({tre},row,{row}).rule);
            const auto shown=ed::display(tre,row.labels);
            const auto prefix=factor==1?"":std::string(ed::multiple_word(factor))+" ";
            check(shown[0]==prefix+"Tre Flip" && shown[4].empty());
            check(ed::replace_completed_text(row.labels[4]+" Flip",{tre})==prefix+"Tre Flip");
        }
        row.labels[4]="540";check(!ed::match({tre},row,{row}).rule);
        row.labels[4]="720";row.labels[2]="180";row.labels[3]="Nollie";
        auto shown=ed::display(tre,row.labels);check(shown[0]=="Double Tre Flip" && shown[2]=="180" && shown[3]=="Nollie");
        check(ed::replace_completed_text("Nollie BS 180 720 Flip",{tre})=="Nollie BS 180 Double Tre Flip");
        tre.stance_prefix=false;check(ed::replace_completed_text("Nollie BS 180 720 Flip",{tre})=="BS 180 Double Tre Flip");tre.stance_prefix=true;
        tre.name="Nollie Tre Flip";shown=ed::display(tre,row.labels);check(shown[0]=="Nollie Double Tre Flip" && shown[3].empty());tre.name="Tre Flip";
        ed::Rule exact=tre;exact.id=2;exact.rotation_multiples=false;exact.first.held="720";exact.name="Exact override";
        check(ed::match({tre,exact},row,{row}).rule->id==2);
        check(ed::replace_completed_text("720 Flip",{tre,exact})=="Exact override");
        check(!ed::conflicting_conditions(tre,exact));
        ed::Rule kick=tre;kick.first={"Kickflip","","",{}};kick.name="Custom Kickflip";
        row.labels={"Kickflip"};
        for(unsigned factor=2;factor<=9;++factor) {
            row.labels[4]=ed::multiple_word(factor);check(ed::match({kick},row,{row}).rule);
            check(ed::display(kick,row.labels)[0]==std::string(ed::multiple_word(factor))+" Custom Kickflip");
            check(ed::replace_completed_text(std::string(ed::multiple_word(factor))+" Kickflip",{kick})==std::string(ed::multiple_word(factor))+" Custom Kickflip");
        }
        row.labels[4]="720";check(!ed::match({kick},row,{row}).rule); // no numeric-axis assumption for ordinary kickflips
        row.labels[4]="Quadruple";check(ed::display(kick,row.labels)[0]=="Quad Custom Kickflip");
        kick.rotation_multiples=false;check(!ed::match({kick},row,{row}).rule);
        auto saved=ed::encode_rules({tre});check(ed::encode_rules(ed::decode_rules(saved)).dump()==saved.dump());
        saved["rules"][0].erase("rotation_multiples");check(!ed::decode_rules(saved)[0].rotation_multiples);
        auto invalid=tre;invalid.combo=true;invalid.spin=180;invalid.direction="BS";bool rejected{};try{ed::validate(invalid);}catch(...){rejected=true;}check(rejected);
        ed::Component numeric=tre.first;check(!ed::held_multiple(numeric,"not-a-count"));check(!ed::held_multiple(numeric,"184467440737095516160"));
    }
    {
        namespace ed=dingosdk::trick_terminology::editor;
        // The native history must keep its surrounding trick order after insertion.
        std::vector<std::uint64_t> order{11,22};
        order.insert(order.begin()+combo_rows::insertion_index(order,22),33);
        order.push_back(44);check(order==std::vector<std::uint64_t>({11,22,33,44}));
        order.insert(order.begin()+combo_rows::insertion_index(order,11),55);
        check(order==std::vector<std::uint64_t>({11,55,22,33,44}));
        check(combo_rows::insertion_index(order,999)==order.size());
        check(!ed::encode_detection(ed::decode_detection(dingosdk::Json::object())).contains("native_combo_rows"));
    }
    for(const auto id:{"ID_TRICK_NOCOMPLY","ID_TRICK_OLLIE"})for(int spin:{0,180,360,540,720}) {
        const auto native=spin?std::to_string(spin-180):std::string{};
        const auto source=spin==180?std::string{}:native;
        const auto corrected=no_comply_display_spin(id,source,spin,true);
        check(corrected==(std::string_view(id)=="ID_TRICK_NOCOMPLY" && spin?std::to_string(spin):source));
        check(no_comply_display_spin(id,source,spin,true)==corrected); // repeated original input cannot stack the correction
        check(no_comply_display_spin(id,source,spin,false)==source);
    }
    for(const auto stance:{"","Nollie","Fakie","Switch"})for(const auto nc_direction:{"FS","BS"}) {
        std::array<std::string,13> nc_labels{};nc_labels[0]="No Comply";nc_labels[1]=nc_direction;nc_labels[2]="180";nc_labels[3]=stance;const auto original=nc_labels;
        nc_labels[2]=no_comply_display_spin("ID_TRICK_NOCOMPLY",nc_labels[2],180,true);
        check(nc_labels[2]=="360" && nc_labels[0]==original[0] && nc_labels[1]==original[1] && nc_labels[3]==original[3]);
    }
    {
        TextEdit edit;std::string physical="180";
        for(int i=0;i<20;++i){const auto original=edit.observe(physical);const auto corrected=no_comply_display_spin("ID_TRICK_NOCOMPLY",original,180,true);if(corrected!=physical){edit.applied(original,corrected);physical=corrected;}check(physical=="360");}
        check(edit.observe(physical)=="180");
        check(no_comply_display_spin("ID_TRICK_NOCOMPLY","0",0,true)=="0");
        check(no_comply_display_spin("ID_TRICK_NOCOMPLY","0",180,true)=="180");
        check(no_comply_display_spin("ID_TRICK_OLLIE","180",180,true)=="180");
    }
    std::cout << "Terminology matching and revision restoration passed\n";
}
