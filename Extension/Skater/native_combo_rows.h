#pragma once
#include "Extension/UI/NativeMenu/native_menu_data.h"
#include <algorithm>
#include <set>
#include <tuple>
#include <optional>

namespace dingosdk::trick_terminology::combo_rows {
using namespace multiplayer::menu_data;
inline constexpr unsigned content=0x716496c8,items=0x61742cb4;
inline constexpr std::uint32_t stack_schema=3609125025u,row_schema=3210643990u;
struct Request {Value source;Handle parent{},after{};std::uint64_t rule{};std::string name;std::vector<Handle> chronological;std::string stamp;};
struct Entry {Request request;Value row,item;std::uint32_t key{};};
struct Event {std::string action,message;Handle source{},parent{},row{};std::uint64_t rule{};};
// Infer chronology from matched native identities, not widget layout direction.
inline std::optional<bool> newest_first(const std::vector<Handle>& existing,const std::vector<Handle>& chronological) {
    std::optional<std::size_t> previous;std::optional<bool> result;
    for(auto handle:chronological) {
        const auto found=std::find(existing.begin(),existing.end(),handle);if(found==existing.end())continue;
        const auto index=static_cast<std::size_t>(found-existing.begin());
        if(previous && *previous!=index) {
            const bool descending=*previous>index;
            if(result && *result!=descending)return {};result=descending;
        }
        previous=index;
    }
    return result;
}
inline std::size_t insertion_index(const std::vector<Handle>& existing,Handle anchor,bool newest=false) {
    const auto found=std::find(existing.begin(),existing.end(),anchor);
    return found==existing.end()?existing.size():static_cast<std::size_t>(found-existing.begin())+(newest?0:1);
}
inline int remapped_index(const std::vector<Handle>& before,const std::vector<Handle>& after,int index) {
    if(index<0 || static_cast<std::size_t>(index)>=before.size())return index;
    const auto found=std::find(after.begin(),after.end(),before[static_cast<unsigned>(index)]);
    return found==after.end()?0:static_cast<int>(found-after.begin());
}
inline bool keep_confirmed(bool same_rule,bool anchor_live) {return same_rule && anchor_live;}
// Native scalar ABI, including four-byte enums used by the ticker.
// Do not accept strings, structures, arrays, references or delegates as numbers.
inline bool zeroable_scalar(unsigned kind,unsigned size) {
    switch(kind) {
    case 8:return size==4;
    case 10:case 11:case 12:return size==1;
    case 13:case 14:return size==2;
    case 15:case 16:case 19:return size==4;
    case 17:case 18:case 20:return size==8;
    default:return false;
    }
}
inline constexpr std::array<std::string_view,17> label_fields{
    "TrickName","BoardDirection","SkaterRotation","TrickStance","HeldRotations","LateTrick","Nollie",
    "SkaterFlipRotation","SkaterFlipDirection","IsDarkCatch","IsDarkFlip","ManualType","IsUnderflip",
    "TrickMetaData","TrickCompletedInfo","TrickScore","SlamDamageAmount"};
inline constexpr std::array<std::string_view,4> scalar_fields{
    "TrickScoreValue","NumSkaterRotations","NumHeldRotations","IsSlam"};
inline unsigned retry_delay(std::string_view message) {
    // Missing source item is transient while native publication finishes.
    // A schema failure should not allocate/destroy models every frame.
    return message=="Combo source is not in its native history stack" || message=="Combo row anchor has left history"?32:1000;
}
class Presenter {
    std::vector<Entry> owned_;
    std::uint64_t serial_{};
    struct Retired {Entry entry;std::uint64_t after{};};
    std::vector<Retired> retired_;
    std::map<Handle,bool> order_;
    std::map<std::pair<Handle,std::uint64_t>,std::uint64_t> retry_;
    static std::uint32_t hash(std::string_view s){
        if(s=="Text")return 0x4d8e01b9u;
        if(s=="ParentStackContext")return 0xb99b6691u;
        if(s=="TrickScoreValue")return 0xc5483d73u;
        if(s=="TrickImg")return 0xc9d594c3u;
        if(s=="TrickCompletedInfo")return 0x0205e59cu;
        if(s=="NumSkaterRotations")return 0x6be1143eu;
        if(s=="NumHeldRotations")return 0x95eda884u;
        if(s=="LateTrick")return 0x9a2a9749u;
        if(s=="Nollie")return 0x6cd6a294u;
        if(s=="BoardDirection")return 0xf5cd4270u;
        if(s=="HeldRotations")return 0xcf29a30du;
        if(s=="SkaterRotation")return 0xd813cc83u;
        if(s=="SlamDamageAmount")return 0xfce95488u;
        if(s=="SkaterFlipRotation")return 0xd7a6e0dau;
        if(s=="SkaterFlipDirection")return 0x680e60ddu;
        if(s=="IsSlam")return 0xaf6e13b9u;
        if(s=="IsDarkCatch")return 0xb7d7ac17u;
        if(s=="IsDarkFlip")return 0xe547253fu;
        if(s=="ManualType")return 0x2bad0ea8u;
        if(s=="IsUnderflip")return 0xa2d7acc5u;
        if(s=="TrickScore")return 0xd491db6au;
        if(s=="TrickBackgroundColor")return 0x2d7dec08u;
        if(s=="TrickMetaData")return 0x3c250648u;
        if(s=="TrickStance")return 0x062499e8u;
        if(s=="TrickName")return 0x53bac52du;
        if(s=="TrickCriteriaMet")return 0xa39e4e77u;
        if(s=="Key")return 0x1e95752cu;
        if(s=="Priority")return 0x644a8875u;
        if(s=="BadgeData")return 0xfc2e9863u;
        if(s=="TabData")return 0x0791c4f7u;
        if(s=="OnDestroy")return 0xb4e604aeu;
        if(s=="OnTransitionStart")return 0x8fc08de3u;
        if(s=="OnTransitionEnd")return 0x0812d719u;
        if(s=="StackInterpolation")return 0x8c0583eeu;
        if(s=="ActiveInterpolation")return 0xc5c7cc11u;
        if(s=="Transition")return 0xd78d240bu;
        if(s=="Duration")return 0x99f9d6eeu;
        if(s=="Content")return 0x716496c8u;
        if(s=="IsActive")return 0x369babfeu;
        if(s=="FocusDirection")return 0xc8765b60u;
        if(s=="FocusedIndex")return 0x55511280u;
        if(s=="IsInteractable")return 0x3b14d43cu;
        if(s=="Margin")return 0xd9fc931cu;
        if(s=="ReverseLayout")return 0x77ae05cau;
        if(s=="DefaultTransitionStyle")return 0x54c9abeeu;
        if(s=="UsePool")return 0x5787da0cu;
        if(s=="DestroyWhenInactive")return 0x10db7267u;
        if(s=="ForceTransitionStyle")return 0x4c3d08f3u;
        if(s=="IsParentFocusTarget")return 0x1fbbb5dau;
        if(s=="StackIndex")return 0x49f87835u;
        if(s=="FocusNavigation")return 0xf97621f9u;
        if(s=="ClearOnDestroy")return 0x2a98baa3u;
        if(s=="Precedence")return 0x3b5ef990u;
        if(s=="LayoutInterpolationDuration")return 0xf06b10a3u;
        if(s=="Orientation")return 0x4cb61cacu;
        if(s=="MaxFocusedItemCount")return 0x6a93bc8au;
        if(s=="Items")return 0x61742cb4u;
        if(s=="LazyInit")return 0xc912aa1du;
        if(s=="InterpolateTabs")return 0x3889134bu;
        if(s=="TargetIndex")return 0x18f8355bu;
        throw std::runtime_error("Unknown native combo row field");
    }
    static Value member(const Context& c,Value v,std::string_view name){return c.field(v,hash(name));}
    static void zero_scalar(const Context& c,Value row,std::string_view name) {
        const auto v=member(c,row,name);const auto k=kind(v.type),n=size(v.type);
        if(!zeroable_scalar(k,n))throw std::runtime_error("Combo row field "+std::string(name)+" has unsupported scalar kind "+std::to_string(k)+", size "+std::to_string(n));
        const std::array<std::byte,8> zero{};c.publish(v,zero.data());
        std::array<std::byte,8> actual{};
        if(!memory::peek_bytes(c.address(v),actual.data(),n) || actual!=zero)throw std::runtime_error("Combo row zero read-back failed for "+std::string(name));
    }
    static Value list(const Context& c,Handle parent) {
        const Value stack{parent,c.type_of(parent)};
        require(stack.type && read<std::uint32_t>(read<Address>(stack.type))==stack_schema,"Combo row parent is not the native history stack");
        return c.field(stack,items);
    }
    static std::vector<Handle> handles(const Context& c,Value array,unsigned count) {
        std::vector<Handle> out;out.reserve(count);
        for(unsigned i=0;i<count;++i)out.push_back(read<Widget>(c.address(c.field(c.element(array,i),content))).data.handle);
        return out;
    }
    // Hold native typed copies of every item until the destination publish
    // completes. Borrowed bytes from the old array do not own nested references.
    void publish_order(const Context& c,Handle parent,const std::vector<Value>& source) {
        const auto target=list(c,parent);unsigned count{},stride{};(void)c.array(target,256,count,stride);
        const auto before=handles(c,target,count);std::vector<Handle> after;
        for(const auto item:source)after.push_back(read<Widget>(c.address(c.field(item,content))).data.handle);
        std::vector<std::pair<Value,int>> indices;
        const Value stack{parent,c.type_of(parent)};
        for(const auto name:{"FocusedIndex","TargetIndex"}) {
            const auto field=member(c,stack,name);
            if((kind(field.type)==15 || kind(field.type)==16) && size(field.type)==4) {
                const auto old=read<int>(c.address(field));const auto next=remapped_index(before,after,old);
                if(next!=old)indices.push_back({field,next});
            }
        }
        std::vector<Value> held;held.reserve(source.size());
        const auto release=[&] {for(auto it=held.rbegin();it!=held.rend();++it)try{c.destroy(*it);}catch(...){}};
        try {
            for(const auto value:source) {
                const auto copy=c.create(stack_item,0x544d535000000000ULL+(++serial_));held.push_back(copy);
                c.copy(copy,c.address(value));
            }
            std::vector<std::byte> bytes(held.size()*stack_item.size);
            for(unsigned i=0;i<held.size();++i)require(memory::peek_bytes(c.address(held[i]),bytes.data()+i*stack_item.size,stack_item.size),"Combo row owned item unavailable");
            c.array(target,bytes,static_cast<unsigned>(held.size()));
            for(const auto& [field,index]:indices)c.set(field,index);
        }catch(...){release();throw;}
        release();
    }
    bool unlink(const Context& c,const Entry& entry) {
        if(!c.type_of(entry.request.parent))return true;
        const auto array=list(c,entry.request.parent);unsigned count{},stride{};(void)c.array(array,256,count,stride);
        require(stride==stack_item.size,"Combo row history item layout differs");
        const auto ids=handles(c,array,count);std::vector<Value> source;
        for(unsigned i=0;i<count;++i)if(ids[i]!=entry.row.handle)source.push_back(c.element(array,i));
        if(source.size()!=count)publish_order(c,entry.request.parent,source);
        return true;
    }
    static void destroy(const Context& c,Entry& entry) {
        if(entry.item.handle && c.type_of(entry.item.handle)==entry.item.type)c.destroy(entry.item);
        if(entry.row.handle && c.type_of(entry.row.handle)==entry.row.type)c.destroy(entry.row);
        entry.item={};entry.row={};
    }
    void append(const Context& c,Entry& entry) {
        const auto array=list(c,entry.request.parent);unsigned count{},stride{};(void)c.array(array,256,count,stride);
        require(stride==stack_item.size && count<256,"Combo row history is full or differs");
        const auto ids=handles(c,array,count);
        if(std::find(ids.begin(),ids.end(),entry.row.handle)!=ids.end())return;
        require(std::find(ids.begin(),ids.end(),entry.request.after)!=ids.end(),"Combo row anchor has left history");
        if(const auto direction=newest_first(ids,entry.request.chronological))order_[entry.request.parent]=*direction;
        require(order_.contains(entry.request.parent),"Combo row native chronology not established");
        const auto at=insertion_index(ids,entry.request.after,order_.at(entry.request.parent));
        std::vector<Value> source;source.reserve(count+1);
        for(unsigned i=0;i<=count;++i) {if(i==at)source.push_back(entry.item);if(i<count)source.push_back(c.element(array,i));}
        publish_order(c,entry.request.parent,source);
    }
public:
    bool contains(Handle row)const{return std::any_of(owned_.begin(),owned_.end(),[&](const Entry& e){return e.row.handle==row;}) || std::any_of(retired_.begin(),retired_.end(),[&](const Retired& e){return e.entry.row.handle==row;});}
    bool empty()const{return owned_.empty() && retired_.empty();}
    void forget(){owned_.clear();retired_.clear();retry_.clear();order_.clear();}
    std::vector<Event> service(const Context& c,const std::vector<Request>& requests,const std::map<std::uint64_t,std::string>& active_rules) {
        std::vector<Event> events;
        const auto now=GetTickCount64();
        for(auto it=retired_.begin();it!=retired_.end();) {
            if(now<it->after){++it;continue;}
            try{destroy(c,it->entry);it=retired_.erase(it);}catch(...){it->after=now+500;++it;}
        }
        for(auto it=owned_.begin();it!=owned_.end();) {
            bool anchor_live{};
            const auto rule=active_rules.find(it->request.rule);
            const bool same_rule=rule!=active_rules.end() && rule->second==it->request.stamp;
            if(same_rule && c.type_of(it->request.source.handle)==it->request.source.type)try {
                const auto array=list(c,it->request.parent);unsigned count{},stride{};(void)c.array(array,256,count,stride);
                const auto ids=handles(c,array,count);anchor_live=std::find(ids.begin(),ids.end(),it->request.after)!=ids.end();
            }catch(...){}
            if(keep_confirmed(same_rule,anchor_live)){++it;continue;}
            try {
                unlink(c,*it);
                events.push_back({"retired","Unlinked native row; deferred model release",it->request.source.handle,it->request.parent,it->row.handle,it->request.rule});
                retired_.push_back({std::move(*it),now+500});it=owned_.erase(it);
            }catch(const std::exception& e){events.push_back({"remove_failed",e.what(),it->request.source.handle,it->request.parent,it->row.handle,it->request.rule});++it;}
        }
        for(const auto& request:requests) {
            const auto retry_key=std::pair{request.source.handle,request.rule};
            if(const auto found=retry_.find(retry_key);found!=retry_.end() && now<found->second)continue;
            try {
                require(!request.name.empty() && request.name.size()<=192,"Combo row name differs");
                auto found=std::find_if(owned_.begin(),owned_.end(),[&](const Entry& e){return e.request.source.handle==request.source.handle && e.request.parent==request.parent && e.request.rule==request.rule;});
                if(found!=owned_.end()) {
                    if(found->request.name!=request.name) {
                        c.text(c.path(found->row,{hash("TrickName"),hash("Text")}),request.name);found->request.name=request.name;
                    }
                    continue;
                }
                require(owned_.size()<64,"Combo row ownership limit reached");
                const auto array=list(c,request.parent);unsigned count{},stride{};
                (void)c.array(array,256,count,stride);require(stride==stack_item.size && count<256,"Combo row list layout differs");
                const auto ids=handles(c,array,count);
                const auto source=std::find(ids.begin(),ids.end(),request.source.handle);
                require(source!=ids.end(),"Combo source is not in its native history stack");
                const auto native_item=c.element(array,static_cast<unsigned>(source-ids.begin()));
                const auto native_widget=read<Widget>(c.address(c.field(native_item,content)));
                require(native_widget.blueprint && native_widget.data.handle==request.source.handle,"Combo row widget differs");
                Entry entry;entry.request=request;
                const auto id=0x5445524d00000000ULL+(++serial_)*2;
                try {
                    require(read<std::uint32_t>(read<Address>(request.source.type))==row_schema,"Combo row template schema differs");
                    entry.row=c.create({row_schema,static_cast<std::uint16_t>(size(request.source.type))},id);
                    c.copy(entry.row,c.address(request.source));
                    for(const auto name:label_fields) {
                        const auto label=member(c,entry.row,name);
                        if(kind(label.type)!=2 || size(label.type)!=104)throw std::runtime_error("Combo row label schema differs for "+std::string(name));
                        c.text(c.field(label,hash("Text")),name=="TrickName"?request.name:"");
                    }
                    for(const auto name:scalar_fields)zero_scalar(c,entry.row,name);
                    c.set(member(c,entry.row,"ParentStackContext"),request.parent);
                    entry.item=c.create(stack_item,id+1);
                    // Copy visual timing only. Native scoring delegates are never cloned.
                    for(const auto name:{"Priority","StackInterpolation","ActiveInterpolation","Transition","Duration"})
                        c.copy(member(c,entry.item,name),c.address(member(c,native_item,name)));
                    entry.key=0x74000000u+static_cast<std::uint32_t>(serial_);
                    for(unsigned i=0;i<count;++i)require(read<std::uint32_t>(c.address(member(c,c.element(array,i),"Key")))!=entry.key,"Combo row key collision");
                    c.set(member(c,entry.item,"Key"),entry.key);
                    c.set(member(c,entry.item,"IsActive"),true);
                    c.set(c.field(entry.item,content),Widget{native_widget.blueprint,{0,entry.row.handle}});
                    append(c,entry);
                    unsigned verified_count{},verified_stride{};
                    (void)c.array(list(c,request.parent),256,verified_count,verified_stride);
                    const auto verified=handles(c,list(c,request.parent),verified_count);
                    require(std::find(verified.begin(),verified.end(),entry.row.handle)!=verified.end(),"Combo row insertion did not persist");
                    require(c.text(c.path(entry.row,{hash("TrickName"),hash("Text")}),1024)==request.name,"Combo row label did not persist");
                    events.push_back({"inserted",order_.at(request.parent)?"Native zero-point row; newest-first history":"Native zero-point row; oldest-first history",request.source.handle,request.parent,entry.row.handle,request.rule});
                    owned_.push_back(std::move(entry));
                }catch(...) {
                    // Unlink before destroying any row that might already have been retained.
                    if(entry.row.handle)unlink(c,entry);
                    retired_.push_back({std::move(entry),now+500});throw;
                }
            }catch(const std::exception& e){
                if(retry_.size()>256)retry_.clear();retry_[retry_key]=now+retry_delay(e.what());
                events.push_back({"insert_failed",e.what(),request.source.handle,request.parent,0,request.rule});
            }
        }
        return events;
    }
};
}
