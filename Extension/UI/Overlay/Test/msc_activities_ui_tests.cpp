// Exercise the actual Activities renderer with both card scopes and ImGui navigation activation.
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include <imgui_internal.h>
#include <cstdio>
#include <stdexcept>
using namespace dingosdk;
using namespace dingosdk::overlay;
namespace {
bool full_screen{};std::string last_text;std::string activate;bool accept_request=true;std::vector<Json> sent;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void activate_id(ImGuiID id){auto& g=*ImGui::GetCurrentContext();g.NavActivateId=id;g.NavActivateDownId=id;g.NavActivatePressedId=id;g.NavInputSource=full_screen?ImGuiInputSource_Gamepad:ImGuiInputSource_Keyboard;}
}
namespace dingosdk::overlay::menu {
float px(float n){return n;}
void section(SkateMenu&,const char* text){ImGui::TextUnformatted(text);}
void note(const char* text){ImGui::TextWrapped("%s",text);}
void begin_card(SkateMenu&,const char* id,const char*,const char*) {
    // Same ID/window boundaries as production begin_card/end_card.
    if(full_screen){ImGui::PushID(id);ImGui::BeginGroup();}
    else ImGui::BeginChild(id,ImVec2(0,450));
    if(std::string_view(id)=="msc-game-current" && !activate.empty())activate_id(ImGui::GetID(activate.c_str()));
}
void end_card(){if(full_screen){ImGui::EndGroup();ImGui::PopID();}else ImGui::EndChild();}
}
int main(){try{
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1600,1200);io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    SkateMenu menu;Model model;msc::Snapshot data;data.epoch=99;data.player=76561198000000001ULL;data.profile.balance=100;
    const auto send=[&](Json j,bool mutation){require(mutation,"Activity was not a mutation");if(accept_request)sent.push_back(j);return accept_request;};
    const auto frame=[&](bool disabled=false,const char* popup_action=nullptr){
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(1400,1100));ImGui::Begin("MSC test host");
        if(popup_action){auto* popup=ImGui::FindWindowByName("Reserve MSC stake");require(popup!=nullptr,"Stake confirmation did not render");activate_id(popup->GetID(popup_action));}
        ImGui::LogToBuffer();menu::draw_msc_games(menu,model,data,disabled,ImGui::GetTime(),send);last_text=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();
        const bool open=ImGui::IsPopupOpen("Reserve MSC stake");ImGui::End();ImGui::Render();activate.clear();return open;
    };
    for(bool style:{false,true})for(const char* game:{"coinflip","dice","highlow"})for(bool joining:{false,true}) {
        if(joining && std::string_view(game)=="highlow")continue;
        std::printf("Case style=%d game=%s join=%d\n",style,game,joining);menu::reset_msc_features();sent.clear();accept_request=true;full_screen=style;
        msc::GameView view;view.id=game;view.label=game;view.enabled=true;view.minimum=10;view.maximum=100;view.capacity=8;view.actions={joining?"join":"create"};
        if(joining){msc::GameRound round;round.id=42;round.creator=data.player+1;round.stake=10;round.pot=10;round.capacity=8;round.seconds=120;view.round=round;}
        data.games={view};frame();
        activate=joining?"Join and reserve equal stake":std::string_view(game)=="highlow"?"Reserve stake and draw first card":"Create lobby and reserve stake";
        require(frame(),"Create/join failed to open stake confirmation in the parent scope");require(sent.empty(),"Stake was sent before confirmation");
        frame();const bool disabled_open=frame(true,"Confirm stake");require(sent.empty(),"Disabled confirmation sent a request");require(disabled_open,"Disabled confirmation closed popup");
        accept_request=false;require(frame(false,"Confirm stake") && sent.empty(),"Rejected queue lost stake confirmation");
        accept_request=true;require(!frame(false,"Confirm stake"),"Accepted confirmation did not close");require(sent.size()==1,"Confirmation did not send exactly once");
        const auto& j=sent.back();require(j.at("op").string()=="game" && j.at("game").string()==game && j.at("action").string()==(joining?"join":"create") && j.at("stake").get<unsigned>()==10 && j.at("round").get<unsigned>()==(joining?42U:0U),"Incorrect activity action mapping");
        frame();require(sent.size()==1,"Idle frame replayed mutation");
        activate=joining?"Join and reserve equal stake":std::string_view(game)=="highlow"?"Reserve stake and draw first card":"Create lobby and reserve stake";
        require(frame(),"Could not reopen stake review");frame();
        if(joining)data.games.front().round->id=43;else ++data.epoch;
        require(frame(false,"Confirm stake") && sent.size()==1,"Stale round/session confirmation dispatched a mutation");
        require(!frame(false,"Cancel") && sent.size()==1,"Cancel sent a stake request or failed to close");data.epoch=99;

    }

    for(bool style:{false,true}) {
        menu::reset_msc_features();sent.clear();full_screen=style;
        msc::GameView view;view.id="slots";view.label="Slots";view.enabled=true;view.minimum=10;view.maximum=100;view.capacity=1;view.actions={"spin"};
        data.games={view};frame();activate="SPIN REELS";frame();
        require(sent.size()==1 && sent[0].at("op").string()=="game" && sent[0].at("game").string()=="slots" && sent[0].at("action").string()=="spin" && sent[0].at("round").get<unsigned>()==0 && sent[0].at("stake").get<unsigned>()==10,"Slot spin did not send a single scoped MSC action");
        msc::GameRound round;round.id=800;round.creator=data.player;round.stake=10;round.pot=10;round.state="settled";round.members={{data.player,50,"Player"}};round.my_payout=50;round.reels={1,1,1};round.has_reels=true;round.multiplier=5;
        data.games.front().previous=round;frame();
        require(last_text.find("Reels spinning")!=std::string::npos && last_text.find("YOU WON!")==std::string::npos,"Slots disclosed the result before reveal");
        io.DeltaTime=4;frame();io.DeltaTime=1.f/60;
        require(last_text.find("YOU WON!")!=std::string::npos && last_text.find("+40 MSC")!=std::string::npos,"Slot reveal omitted the server payout");
    }
    for(bool style:{false,true})for(const char* action:{"start","leave","cancel","higher","lower"}) {
        menu::reset_msc_features();sent.clear();full_screen=style;
        msc::GameView view;const bool high=std::string_view(action)=="higher" || std::string_view(action)=="lower";
        view.id=high?"highlow":"coinflip";view.label=view.id;view.enabled=true;view.minimum=10;view.maximum=100;view.actions={action};
        msc::GameRound round;round.id=99;round.creator=data.player;round.stake=10;round.pot=20;round.initial=7;round.seconds=120;view.round=round;data.games={view};frame();
        activate=std::string_view(action)=="start"?"Start game":std::string_view(action)=="leave"?"Leave and refund stake":std::string_view(action)=="cancel"?"Cancel lobby and refund everyone":action;
        frame();require(sent.size()==1 && sent[0].at("action").string()==action && sent[0].at("round").get<unsigned>()==99,"Activity round button did not dispatch its exact action");
    }

    for(bool style:{false,true}) {
        menu::reset_msc_features();full_screen=style;data.player=76561198000000001ULL;
        msc::GameView view;view.id="dice";view.label="Dice Duel";view.enabled=true;
        msc::GameRound round;round.id=200;round.stake=10;round.pot=20;round.creator=data.player;round.seconds=120;round.state="pending";
        round.members={{data.player,20,"Dice A"},{data.player+1,0,"Dice B"}};view.round=round;data.games={view};frame();
        round.state="settled";round.winner=data.player;round.my_payout=20;round.rolls={{1,data.player,6},{1,data.player+1,2}};
        data.games.front().round.reset();data.games.front().previous=round;frame();
        require(last_text.find("rolling")!=std::string::npos && last_text.find("YOU WON")==std::string::npos,"Dice reveal disclosed winner early");
        require(last_text.find("Dice A")!=std::string::npos && last_text.find("Dice B")==std::string::npos,"Dice did not reveal individually");
        io.DeltaTime=2;frame();io.DeltaTime=1.f/60;
        require(last_text.find("YOU WON!")!=std::string::npos && last_text.find("+10 MSC")!=std::string::npos,"Winner banner/net gain missing");
        round.id=201;round.my_payout=0;data.games.front().previous=round;frame();require(last_text.find("YOU LOST")!=std::string::npos && last_text.find("-10 MSC")!=std::string::npos,"Loss banner/net loss missing");
        round.id=202;round.my_payout=10;round.state="refunded";data.games.front().previous=round;frame();require(last_text.find("STAKE RETURNED")!=std::string::npos,"Refund banner missing");
    }
    menu::reset_msc_features();ImGui::DestroyContext();std::puts("MSC activity UI tests passed (both menu scopes, create/join, confirmation and queue failure)");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
