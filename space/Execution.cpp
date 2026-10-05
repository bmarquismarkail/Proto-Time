#include "Execution.hpp"
#include <chrono>
namespace BMMQ::Space {
namespace {constexpr const char* pairs[]={"AF","BC","DE","HL","SP","PC"};}
Execution::Block::Block(){
    for(size_t i=0;i<6;++i)snapshot.file.addRegister(pairs[i],i<4);
    snapshot.mem.reserve(8320,8320);instructions.reserve(2048);
}
Execution::Execution(GB::GameBoyMachine& machine,Capture& capture,std::shared_ptr<StateBudget> budget)
 :machine_(machine),capture_(capture),budget_(budget?std::move(budget):capture.budget),canonical_(machine.executionMemory()),
  bus_(dynamic_cast<GB::GameBoyMemoryMap&>(canonical_.backingStore())){}
Execution::~Execution(){clear();}
void Execution::clear(){
    active_=nullptr;pending_.reset();blocks_.clear();owners_.clear();values_.clear();values_.shrink_to_fit();registers_={};
    if(charged_)budget_->release(charged_);
    charged_=0;
}
uint16_t Execution::normalize(uint16_t a){return a>=0xe000&&a<=0xfdff?uint16_t(a-0x2000):a;}
bool Execution::ram(uint16_t a){a=normalize(a);return (a>=0xc000&&a<0xe000)||(a>=0xff80&&a<0xffff);}
void Execution::mode(const std::string& mode){
    if(mode!="baseline"&&mode!="snapshot")throw std::invalid_argument("execution mode must be baseline or snapshot");
    requestedMode_=mode;
    if(mode=="baseline"){clear();snapshot_=false;reason_.clear();return;}
    if(capture_.stopped())pause("snapshot execution requires complete ongoing capture");
    clear();snapshot_=false;
    if(!budget_->reserve(baseCharge))pause("execution storage exhausted");
    charged_=baseCharge;
    try{values_.resize(65536);}catch(...){clear();throw;}
    visits_=sequence_=0;epochId_=std::chrono::steady_clock::now().time_since_epoch().count();epoch_=decimal(epochId_);
    snapshot_=true;reason_.clear();
}
[[noreturn]] void Execution::pause(const char* reason){reason_=reason;throw ExecutionPaused(reason);}
void Execution::preflight(){
    executing_=false;
    if(!reason_.empty())throw ExecutionPaused(reason_);
    if(!snapshot_)return;
    if(active_&&active_->instructions.size()>=2048)pause("execution block instruction limit; explicit baseline required");
    if(capture_.stopped())pause("capture evidence lost or exhausted; explicit baseline required");
    if(!machine_.snapshotBoundaryOnly()){
        const auto pc=canonical_.file.findRegister("PC")->reg->value;
        const auto space=bus_.analysisLocation(pc)>>32;
        if(space!=1&&space!=2&&!ram(pc))pause("unsupported instruction backing; explicit baseline required");
        auto phase=capture_.phase;capture_.phase=Kind::Inspection;auto opcode=bus_.read(pc);capture_.phase=phase;
        if(capture_.stopped())pause("capture evidence lost; explicit baseline required");
        if(!machine_.snapshotOpcodeSupported(opcode))pause("unsupported opcode; explicit baseline required");
    }
    if(pending_)return;
    // Reserve the worst case before fetch/interrupt entry can have guest effects.
    if(!budget_->reserve(blockCharge))pause("execution storage exhausted; explicit baseline required");
    charged_+=blockCharge;
    try{pending_=std::make_unique<Block>();}catch(...){budget_->release(blockCharge);charged_-=blockCharge;throw;}
}
uint16_t Execution::lane(size_t n,const RegisterFile<uint16_t>& file)const{
    if(n<8){auto v=file.findRegister(pairs[n/2])->reg->value;return n%2?v&255:v>>8;}
    return file.findRegister(pairs[n-4])->reg->value;
}
IMemory<uint16_t,uint8_t,uint16_t>* Execution::begin(std::span<const uint8_t> bytes,uint64_t location,
        MemoryPool<uint16_t,uint8_t,uint16_t>&,bool blocked){
    if(!snapshot_||bytes.empty())return nullptr;
    if(bytes.size()>3)pause("unsupported instruction length");
    Key key;key.location=location;key.length=bytes.size();std::copy(bytes.begin(),bytes.end(),key.bytes.begin());
    auto found=owners_.find(key);
    if(found!=owners_.end()){
        auto [id,index]=found->second;
        if(index && (!active_||active_->id!=id)){
            // New interior entry partitions instruction ownership; suppliers live independently.
            pending_->id=blocks_.size()+1;
            auto& prior=*blocks_.at(id-1);
            pending_->instructions.assign(prior.instructions.begin()+index,prior.instructions.end());
            prior.instructions.resize(index);
            for(size_t i=0;i<pending_->instructions.size();++i)owners_[pending_->instructions[i]]={pending_->id,i};
            blocks_.push_back(std::move(pending_));active_=blocks_.back().get();
        }else active_=blocks_.at(id-1).get();
    }else{
        if(!active_){pending_->id=blocks_.size()+1;blocks_.push_back(std::move(pending_));active_=blocks_.back().get();}
        if(active_->instructions.size()>=2048)pause("execution block instruction limit; explicit baseline required");
        owners_[key]={active_->id,active_->instructions.size()};active_->instructions.push_back(key);
    }
    if(pending_){pending_.reset();budget_->release(blockCharge);charged_-=blockCharge;}
    ramBlocked_=blocked;executing_=true;active_->visit=++visits_;
    auto& file=active_->snapshot.file;
    for(size_t i=0;i<10;++i){auto actual=lane(i,canonical_.file);
        if(!registers_[i].known)registers_[i]={0,0,actual,true,epochId_};
        else if(registers_[i].value!=actual)registers_[i]={++sequence_,0,actual,true,epochId_};}
    for(size_t i=0;i<6;++i)file.findRegister(pairs[i])->reg->value=i<4?uint16_t((registers_[2*i].value<<8)|registers_[2*i+1].value):registers_[i+4].value;
    operands_=decode(bytes,canonical_.file.findRegister("PC")->reg->value,0,canonical_.file.findRegister("AF")->reg->value&255);
    return this;
}
RegisterFile<uint16_t>& Execution::executionRegisters(){return active_->snapshot.file;}
void Execution::publishRegisters(){
    if(!active_)return;
    for(auto name:pairs)canonical_.file.findRegister(name)->reg->value=active_->snapshot.file.findRegister(name)->reg->value;
}
void Execution::canonicalWrite(uint16_t address,uint8_t value){
    if(!snapshot_||!ram(address))return;
    values_[normalize(address)]={++sequence_,executing_&&active_?active_->id:0,value,true,epochId_};
}
void Execution::traceRead(uint16_t address,uint8_t value,uint8_t source,const Cell& supplier){
    Record record;record.kind=Kind::Read;record.address=address;record.location=bus_.analysisLocation(address);record.value=value;
    record.executionSource=source;record.supplierSequence=supplier.sequence;record.supplierBlock=supplier.block;record.supplierEpoch=supplier.epoch;capture_.push(record);
}
void Execution::read(std::span<uint8_t> output,uint16_t address){
    publishRegisters();
    bool supported=true;
    for(size_t i=0;i<output.size();++i){auto a=normalize(uint16_t(address+i));supported=supported&&ram(a)&&(!ramBlocked_||a>=0xff80);}
    if(!supported){busReads_+=output.size();canonical_.read(output,address);return;}
    for(size_t i=0;i<output.size();++i){auto a=uint16_t(address+i),normalized=normalize(a);
        if(ram(a)&&(!ramBlocked_||normalized>=0xff80)){
            auto& supplier=values_[normalized];uint8_t source=1;
            if(!supplier.known){auto phase=capture_.phase;capture_.phase=Kind::Inspection;
                auto value=bus_.read(normalized);capture_.phase=phase;supplier={0,0,value,true,epochId_};source=2;}
            uint8_t byte=static_cast<uint8_t>(supplier.value);
            // Refreshing this local read capture does not change supplier ownership.
            active_->snapshot.mem.write(std::span(&byte,1),normalized);
            active_->snapshot.read(output.subspan(i,1),normalized);
            ++snapshotReads_;traceRead(a,output[i],source,supplier);
        }
    }
}
void Execution::write(std::span<const uint8_t> input,uint16_t address){
    publishRegisters();
    // Preserve span semantics of the baseline interceptor (including two-byte boundary writes).
    canonical_.write(input,address);
    for(size_t i=0;i<input.size();++i){auto a=normalize(uint16_t(address+i));if(ram(a)&&values_[a].known){uint8_t value=values_[a].value;active_->snapshot.mem.write(std::span(&value,1),a);}}
}
void Execution::retired(Record& record){
    if(!record.length){++boundaries_;active_=nullptr;
        if(snapshot_)for(size_t i=0;i<10;++i){auto actual=lane(i,canonical_.file);if(!registers_[i].known)registers_[i]={0,0,actual,true,epochId_};
        else if(registers_[i].value!=actual)registers_[i]={++sequence_,0,actual,true,epochId_};}
    }
    else if(snapshot_){
        ++snapshotInstructions_;record.executionSource=1;
        for(size_t i=0;i<10;++i){record.registerSuppliers[i]=registers_[i].sequence;record.registerSupplierBlocks[i]=registers_[i].block;record.registerSupplierEpochs[i]=registers_[i].epoch;
            if(operands_.writes&(1<<i))registers_[i]={++sequence_,active_->id,lane(i,canonical_.file),true,epochId_};}
        if(operands_.flow!="fallthrough")active_=nullptr;
    }else ++baselineInstructions_;
    executing_=false;
    if(pending_){pending_.reset();budget_->release(blockCharge);charged_-=blockCharge;}
}
Json Execution::status()const{return {{"requestedMode",requestedMode_},{"activeMode",reason_.empty()?(snapshot_?"snapshot":"baseline"):"paused"},
    {"pauseReason",reason_},{"epoch",epoch_},{"snapshotInstructions",decimal(snapshotInstructions_)},{"baselineInstructions",decimal(baselineInstructions_)},
    {"authoritativeBoundarySteps",decimal(boundaries_)},{"snapshotReads",decimal(snapshotReads_)},{"busReads",decimal(busReads_)},{"executionBytes",decimal(charged_)}};}
Json Execution::keyJson(const Key& key){return {{"location",decimal(key.location)},{"bytes",key.bytes},{"length",key.length}};}
Execution::Key Execution::parseKey(const Json& j){Key key;key.location=counter(j.at("location"));auto space=key.location>>32;
    auto bank=uint16_t(key.location>>16),offset=uint16_t(key.location);
    if(!((space==1&&offset<0x4000)||(space==2&&bank==0&&offset<256)||(space==3&&bank==0&&ram(offset)&&normalize(offset)==offset)))throw std::invalid_argument("invalid execution location");
    auto n=j.at("length");if(!n.is_number_integer()||n<1||n>3)throw std::invalid_argument("invalid execution length");key.length=n.get<uint8_t>();
    if(!j.at("bytes").is_array()||j["bytes"].size()!=3)throw std::invalid_argument("invalid execution bytes");
    for(size_t i=0;i<3;++i){auto b=j["bytes"][i];if(!b.is_number_integer()||b<0||b>255)throw std::invalid_argument("invalid execution byte");key.bytes[i]=b.get<uint8_t>();}return key;}
Json Execution::state()const{
    Json out=status();out["schemaVersion"]=1;
    out["epochCounter"]=decimal(epochId_);out["visits"]=decimal(visits_);out["sequence"]=decimal(sequence_);out["activeBlock"]=decimal(active_?active_->id:0);
    out["blocks"]=Json::array();out["values"]=Json::object();out["registers"]=Json::array();
    auto cell=[](const Cell& c){return Json{{"sequence",decimal(c.sequence)},{"block",decimal(c.block)},{"value",c.value},{"known",c.known},{"epoch",decimal(c.epoch)}};};
    for(size_t a=0;a<values_.size();++a)if(values_[a].known)out["values"][decimal(a)]=cell(values_[a]);
    for(auto& c:registers_)out["registers"].push_back(cell(c));
    for(auto& b:blocks_){Json block={{"id",decimal(b->id)},{"visit",decimal(b->visit)},{"instructions",Json::array()},{"bytes",Json::object()},{"registers",Json::object()}};
        for(auto& key:b->instructions)block["instructions"].push_back(keyJson(key));
        // Store normalized captured bytes, including stale views; suppliers are separate.
        auto& storage=b->snapshot.mem;auto& pools=storage.pools();auto& data=storage.data();
        for(size_t p=0;p<pools.size();++p){size_t end=p+1<pools.size()?pools[p+1].second:data.size();for(size_t i=pools[p].second;i<end;++i)block["bytes"][decimal(pools[p].first+i-pools[p].second)]=data[i];}
        for(auto name:pairs)block["registers"][name]=b->snapshot.file.findRegister(name)->reg->value;
        out["blocks"].push_back(std::move(block));}
    return out;
}
void Execution::adoptBudget(std::shared_ptr<StateBudget> budget){
    if(!budget->reserve(charged_))throw std::logic_error("validated execution reservation unavailable");
    budget_->release(charged_);budget_=std::move(budget);
}
void Execution::restore(const Json& j,bool newBranch,bool verifyCanonical){
    if(j.at("schemaVersion")!=1)throw std::invalid_argument("unsupported execution schema");
    auto requested=j.at("requestedMode").get<std::string>();if(requested!="baseline"&&requested!="snapshot")throw std::invalid_argument("invalid execution mode");
    if(!j.at("blocks").is_array()||!j.at("values").is_object()||!j.at("registers").is_array()||j["registers"].size()!=10)throw std::invalid_argument("invalid execution collections");
    auto count=j["blocks"].size();if(count>122)throw std::invalid_argument("execution budget exceeded");
    auto parseCell=[&](const Json& value,unsigned max){Cell c;c.sequence=counter(value.at("sequence"));c.block=counter(value.at("block"));c.known=value.at("known").get<bool>();c.epoch=counter(value.at("epoch"));
        auto n=value.at("value");if(!n.is_number_integer()||n<0||n>max||c.block>count)throw std::invalid_argument("invalid execution supplier");c.value=n.get<uint16_t>();return c;};
    // This method is called on a disposable controller first by the coordinator.
    mode(requested);reason_=j.at("pauseReason").get<std::string>();epoch_=j.at("epoch").get<std::string>();epochId_=counter(j.at("epochCounter"));
    visits_=counter(j.at("visits"));sequence_=counter(j.at("sequence"));
    snapshotInstructions_=counter(j.at("snapshotInstructions"));baselineInstructions_=counter(j.at("baselineInstructions"));boundaries_=counter(j.at("authoritativeBoundarySteps"));snapshotReads_=counter(j.at("snapshotReads"));busReads_=counter(j.at("busReads"));
    if(epoch_!=decimal(epochId_)&&!(epoch_.empty()&&epochId_==0))throw std::invalid_argument("execution epoch mismatch");
    if(j.at("activeMode")!=(reason_.empty()?requested:"paused")||counter(j.at("executionBytes"))!=(snapshot_?baseCharge+count*blockCharge:0))throw std::invalid_argument("execution status mismatch");
    if(!snapshot_&&(!j["blocks"].empty()||!j["values"].empty()))throw std::invalid_argument("baseline checkpoint contains execution snapshots");
    for(size_t i=0;i<count;++i){if(!budget_->reserve(blockCharge))throw std::invalid_argument("execution restore exceeds budget");charged_+=blockCharge;
        auto b=std::make_unique<Block>();auto& saved=j["blocks"][i];b->id=counter(saved.at("id"));b->visit=counter(saved.at("visit"));if(b->id!=i+1||b->visit>visits_)throw std::invalid_argument("invalid execution block ID/visit");
        if(!saved.at("instructions").is_array()||saved["instructions"].empty()||saved["instructions"].size()>2048||!saved.at("bytes").is_object()||!saved.at("registers").is_object())throw std::invalid_argument("invalid execution block");
        for(auto& entry:saved["instructions"]){auto key=parseKey(entry);if(owners_.contains(key))throw std::invalid_argument("duplicate execution ownership");owners_[key]={b->id,b->instructions.size()};b->instructions.push_back(key);}
        for(auto it=saved["bytes"].begin();it!=saved["bytes"].end();++it){auto a=counter(Json(it.key()));if(a>65535||!ram(a)||normalize(a)!=a||!it.value().is_number_integer()||it.value()<0||it.value()>255)throw std::invalid_argument("invalid snapshot byte");uint8_t v=it.value().get<uint8_t>();b->snapshot.mem.write(std::span(&v,1),a);}
        for(auto name:pairs){auto v=saved["registers"].at(name);if(!v.is_number_integer()||v<0||v>65535)throw std::invalid_argument("invalid snapshot register");b->snapshot.file.findRegister(name)->reg->value=v.get<uint16_t>();}
        blocks_.push_back(std::move(b));}
    for(auto it=j["values"].begin();it!=j["values"].end();++it){auto a=counter(Json(it.key()));if(a>65535||!ram(a)||normalize(a)!=a)throw std::invalid_argument("invalid supplier address");auto c=parseCell(it.value(),255);
        if(!c.known||(verifyCanonical&&bus_.read(a)!=c.value)||c.sequence>sequence_)throw std::invalid_argument("supplier differs from restored machine");
        values_[a]=c;}
    for(size_t i=0;i<10;++i){registers_[i]=parseCell(j["registers"][i],i<8?255:65535);if(registers_[i].known&&(registers_[i].sequence>sequence_||(verifyCanonical&&registers_[i].value!=lane(i,canonical_.file))))throw std::invalid_argument("register supplier differs from machine");}
    auto active=counter(j.at("activeBlock"));if(active>count)throw std::invalid_argument("invalid active block");active_=active?blocks_[active-1].get():nullptr;
    if(newBranch){epochId_=std::chrono::steady_clock::now().time_since_epoch().count();epoch_=decimal(epochId_);}
}
}
