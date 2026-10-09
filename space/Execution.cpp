#include "Execution.hpp"
#include <chrono>
#include <openssl/sha.h>
namespace BMMQ::Space {
Execution::Block::Block(const CoreModel& model){
    for(size_t i=0;i<model.pairs.size();++i)snapshot.file.addRegister(model.pairs[i],i<4);
    snapshot.mem.reserve(24576,24576);instructions.reserve(2048);
}
Execution::Execution(Machine& machine,Capture& capture,std::shared_ptr<StateBudget> budget)
 :core_(makeCoreAdapter(machine)),model_(core_->model()),capture_(capture),budget_(budget?std::move(budget):capture.budget),canonical_(core_->memory()){}
Execution::~Execution(){clear();}
void Execution::clear(){
    active_=nullptr;pending_.reset();blocks_.clear();owners_.clear();values_.clear();values_.shrink_to_fit();fetchIdentity_.clear();fetchIdentity_.shrink_to_fit();registers_={};
    if(charged_)budget_->release(charged_);
    charged_=0;
}
uint16_t Execution::normalize(uint16_t a)const{return core_->normalize(a);}
bool Execution::ram(uint16_t a)const{return core_->ram(a);}
size_t Execution::baseReservation()const{
    const auto capacity=core_->ramCapacity();
    if(capacity<65536||capacity>65536+0x20000)throw std::invalid_argument("unsupported physical RAM capacity");
    return baseCharge+(capacity-65536)*sizeof(Cell);
}
bool Execution::snapshotAddress(uint16_t a)const{
    if(model_.id=="gameboy")return (a>=0xc000&&a<0xe000)||(a>=0xff80&&a<0xffff)||
        (core_->ramCapacity()>65536&&a>=0xa000&&a<0xc000);
    return (a>=0xc000&&a<0xe000)||(core_->ramCapacity()>65536&&a>=0x8000&&a<0xc000);
}
void Execution::mode(const std::string& mode){
    if(mode!="baseline"&&mode!="snapshot")throw std::invalid_argument("execution mode must be baseline or snapshot");
    requestedMode_=mode;
    if(mode=="baseline"){clear();snapshot_=false;reason_.clear();return;}
    if(capture_.stopped())pause("snapshot execution requires complete ongoing capture");
    clear();snapshot_=false;
    const auto charge=baseReservation();
    if(!budget_->reserve(charge))pause("execution storage exhausted");
    charged_=charge;
    try{values_.resize(core_->ramCapacity());fetchIdentity_.resize(size_t(model_.maximumInstructionLength)*9);}catch(...){clear();throw;}
    visits_=sequence_=0;epochId_=std::chrono::steady_clock::now().time_since_epoch().count();epoch_=decimal(epochId_);
    snapshot_=true;reason_.clear();
}
[[noreturn]] void Execution::pause(const char* reason){reason_=reason;throw ExecutionPaused(reason);}
void Execution::preflight(){
    executing_=false;
    if(!reason_.empty())throw ExecutionPaused(reason_);
    if(!snapshot_)return;
    if(values_.size()!=core_->ramCapacity())pause("physical RAM layout changed; explicit baseline required");
    if(active_&&active_->instructions.size()>=2048)pause("execution block instruction limit; explicit baseline required");
    if(capture_.stopped())pause("capture evidence lost or exhausted; explicit baseline required");
    if(!core_->boundaryOnly()){
        const auto pc=canonical_.file.findRegister("PC")->reg->value;
        const auto space=core_->location(pc)>>32;
        if(space!=1&&space!=2&&!ram(pc))pause("unsupported instruction backing; explicit baseline required");
        auto phase=capture_.phase;capture_.phase=Kind::Inspection;auto opcode=core_->rawRead(pc);capture_.phase=phase;
        if(capture_.stopped())pause("capture evidence lost; explicit baseline required");
        if(!core_->opcodeSupported(opcode))pause("unsupported opcode; explicit baseline required");
        try{core_->prepareInstruction();}catch(const ExecutionPaused& e){pause(e.what());}
    }
    if(pending_)return;
    // Reserve the worst case before fetch/interrupt entry can have guest effects.
    if(!budget_->reserve(blockCharge))pause("execution storage exhausted; explicit baseline required");
    charged_+=blockCharge;
    try{pending_=std::make_unique<Block>(model_);}catch(...){budget_->release(blockCharge);charged_-=blockCharge;throw;}
}
uint16_t Execution::lane(size_t n,const RegisterFile<uint16_t>& file)const{
    auto d=model_.lanes[n];auto value=file.findRegister(model_.pairs[d.pair])->reg->value;
    return d.byte?uint16_t(d.high?value>>8:value&255):value;
}
IMemory<uint16_t,uint8_t,uint16_t>* Execution::begin(std::span<const uint8_t> bytes,uint64_t location,
        MemoryPool<uint16_t,uint8_t,uint16_t>&,bool blocked){
    if(!snapshot_||bytes.empty())return nullptr;
    if(bytes.size()>model_.maximumInstructionLength)pause("unsupported instruction length");
    Key key;key.location=location;key.length=bytes.size();std::copy_n(bytes.begin(),std::min<size_t>(3,bytes.size()),key.bytes.begin());
    if(model_.id=="gamegear")SHA256(bytes.data(),bytes.size(),key.codeDigest.data());
    const auto pc=canonical_.file.findRegister("PC")->reg->value;
    for(size_t i=0;i<bytes.size();++i){const auto backing=core_->fetchLocation(pc,i);
        for(unsigned b=0;b<8;++b)fetchIdentity_[i*9+b]=uint8_t(backing>>(b*8));
        fetchIdentity_[i*9+8]=bytes[i];}
    SHA256(fetchIdentity_.data(),bytes.size()*9,key.fetchDigest.data());
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
    for(size_t i=0;i<model_.lanes.size();++i){auto actual=lane(i,canonical_.file);
        if(!registers_[i].known)registers_[i]={0,0,actual,true,epochId_};
        else if(registers_[i].value!=actual)registers_[i]={++sequence_,0,actual,true,epochId_};}
    for(auto name:model_.pairs)file.findRegister(name)->reg->value=canonical_.file.findRegister(name)->reg->value;
    operands_=decodeCore(model_.id,bytes,canonical_.file.findRegister("PC")->reg->value,0,canonical_.file.findRegister("AF")->reg->value&255);
    return this;
}
RegisterFile<uint16_t>& Execution::executionRegisters(){return active_->snapshot.file;}
void Execution::publishRegisters(){
    if(!active_)return;
    for(auto name:model_.pairs)canonical_.file.findRegister(name)->reg->value=active_->snapshot.file.findRegister(name)->reg->value;
}
void Execution::canonicalWrite(uint16_t address,uint8_t value){
    if(!snapshot_||!ram(address))return;
    const auto index=core_->ramIndex(address);
    // Sega controls at FFFC..FFFF also write the work-RAM mirror; cartridge
    // controls elsewhere do not write the RAM visible on their read path.
    if(index>=65536&&(core_->location(address,true)>>32)!=3)return;
    // Canonical writes have completed before this callback. Inspect the stored
    // read value, including MBC2's upper read bits and nibble normalization.
    if(index>=values_.size()||!core_->physicalRamRead(index,value))return;
    values_[index]={++sequence_,executing_&&active_?active_->id:0,value,true,epochId_};
}
void Execution::traceRead(uint16_t address,uint8_t value,uint8_t source,const Cell& supplier){
    Record record;record.kind=Kind::Read;record.address=address;record.location=core_->location(address);record.value=value;
    record.executionSource=source;record.supplierSequence=supplier.sequence;record.supplierBlock=supplier.block;record.supplierEpoch=supplier.epoch;capture_.push(record);
}
void Execution::read(std::span<uint8_t> output,uint16_t address){
    publishRegisters();
    bool supported=true;
    for(size_t i=0;i<output.size();++i){auto a=uint16_t(address+i);supported=supported&&ram(a)&&(!ramBlocked_||normalize(a)>=0xff80);}
    if(!supported){busReads_+=output.size();canonical_.read(output,address);return;}
    for(size_t i=0;i<output.size();++i){auto a=uint16_t(address+i),normalized=normalize(a);
        if(ram(a)&&(!ramBlocked_||normalized>=0xff80)){
            auto& supplier=values_[core_->ramIndex(a)];uint8_t source=1;
            if(!supplier.known){auto phase=capture_.phase;capture_.phase=Kind::Inspection;
                uint8_t value=0;if(!core_->physicalRamRead(core_->ramIndex(a),value)){capture_.phase=phase;pause("physical RAM backing unavailable");}
                capture_.phase=phase;supplier={0,0,value,true,epochId_};source=2;}
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
    for(size_t i=0;i<input.size();++i){auto original=uint16_t(address+i),a=normalize(original);if(ram(original)){
        auto& supplier=values_[core_->ramIndex(original)];if(supplier.known){uint8_t value=supplier.value;active_->snapshot.mem.write(std::span(&value,1),a);}}}
}
void Execution::retired(Record& record){
    if(!record.length){++boundaries_;active_=nullptr;
        if(snapshot_)for(size_t i=0;i<model_.lanes.size();++i){auto actual=lane(i,canonical_.file);if(!registers_[i].known)registers_[i]={0,0,actual,true,epochId_};
        else if(registers_[i].value!=actual)registers_[i]={++sequence_,0,actual,true,epochId_};}
    }
    else if(snapshot_){
        ++snapshotInstructions_;record.executionSource=1;
        for(size_t i=0;i<model_.lanes.size();++i){record.registerSuppliers[i]=registers_[i].sequence;record.registerSupplierBlocks[i]=registers_[i].block;record.registerSupplierEpochs[i]=registers_[i].epoch;
            if((operands_.writes&(1u<<i))||registers_[i].value!=lane(i,canonical_.file))registers_[i]={++sequence_,active_->id,lane(i,canonical_.file),true,epochId_};}
        if(operands_.flow!="fallthrough")active_=nullptr;
    }else ++baselineInstructions_;
    executing_=false;
    if(pending_){pending_.reset();budget_->release(blockCharge);charged_-=blockCharge;}
}
Json Execution::status()const{return {{"requestedMode",requestedMode_},{"activeMode",reason_.empty()?(snapshot_?"snapshot":"baseline"):"paused"},
    {"pauseReason",reason_},{"epoch",epoch_},{"snapshotInstructions",decimal(snapshotInstructions_)},{"baselineInstructions",decimal(baselineInstructions_)},
    {"authoritativeBoundarySteps",decimal(boundaries_)},{"snapshotReads",decimal(snapshotReads_)},{"busReads",decimal(busReads_)},{"executionBytes",decimal(charged_)}};}
Json Execution::keyJson(const Key& key){return {{"location",decimal(key.location)},{"bytes",key.bytes},{"codeDigest",key.codeDigest},{"fetchDigest",key.fetchDigest},{"length",key.length}};}
Execution::Key Execution::parseKey(const Json& j,bool legacy,bool verifyCanonical)const{Key key;key.location=counter(j.at("location"));auto space=key.location>>32;
    auto bank=uint16_t(key.location>>16),offset=uint16_t(key.location);
    uint8_t ignored=0;const auto physical=core_->physicalRamIndex(key.location);
    if(!((space==1&&offset<0x4000)||(space==2&&bank==0&&(model_.id=="gamegear"?offset<0x4000:offset<256))||
        (space==3&&physical<core_->ramCapacity()&&(!verifyCanonical||core_->physicalRamRead(physical,ignored)))))throw std::invalid_argument("invalid execution location");
    auto n=j.at("length");if(!n.is_number_integer()||n<1||n>model_.maximumInstructionLength)throw std::invalid_argument("invalid execution length");key.length=n.get<uint32_t>();
    if(!j.at("bytes").is_array()||j["bytes"].size()!=3)throw std::invalid_argument("invalid execution bytes");
    for(size_t i=0;i<3;++i){auto b=j["bytes"][i];if(!b.is_number_integer()||b<0||b>255)throw std::invalid_argument("invalid execution byte");key.bytes[i]=b.get<uint8_t>();}
    if(model_.id=="gamegear"){if(!j.at("codeDigest").is_array()||j["codeDigest"].size()!=32)throw std::invalid_argument("missing instruction digest");for(size_t i=0;i<32;++i){auto b=j["codeDigest"][i];if(!b.is_number_integer()||b<0||b>255)throw std::invalid_argument("invalid instruction digest");key.codeDigest[i]=b.get<uint8_t>();}}
    if(!legacy||j.contains("fetchDigest")){
        if(!j.at("fetchDigest").is_array()||j["fetchDigest"].size()!=32)throw std::invalid_argument("missing fetch backing digest");
        for(size_t i=0;i<32;++i){auto b=j["fetchDigest"][i];if(!b.is_number_integer()||b<0||b>255)throw std::invalid_argument("invalid fetch backing digest");key.fetchDigest[i]=b.get<uint8_t>();}
    }
    return key;}
Json Execution::state()const{
    Json out=status();out["schemaVersion"]=3;out["core"]=model_.id;out["ramCapacity"]=decimal(core_->ramCapacity());
    out["epochCounter"]=decimal(epochId_);out["visits"]=decimal(visits_);out["sequence"]=decimal(sequence_);out["activeBlock"]=decimal(active_?active_->id:0);
    out["blocks"]=Json::array();out["values"]=Json::object();out["registers"]=Json::array();
    auto cell=[](const Cell& c){return Json{{"sequence",decimal(c.sequence)},{"block",decimal(c.block)},{"value",c.value},{"known",c.known},{"epoch",decimal(c.epoch)}};};
    for(size_t a=0;a<values_.size();++a)if(values_[a].known)out["values"][decimal(a)]=cell(values_[a]);
    for(size_t i=0;i<model_.lanes.size();++i)out["registers"].push_back(cell(registers_[i]));
    for(auto& b:blocks_){Json block={{"id",decimal(b->id)},{"visit",decimal(b->visit)},{"instructions",Json::array()},{"bytes",Json::object()},{"registers",Json::object()}};
        for(auto& key:b->instructions)block["instructions"].push_back(keyJson(key));
        // Store normalized captured bytes, including stale views; suppliers are separate.
        auto& storage=b->snapshot.mem;auto& pools=storage.pools();auto& data=storage.data();
        for(size_t p=0;p<pools.size();++p){size_t end=p+1<pools.size()?pools[p+1].second:data.size();for(size_t i=pools[p].second;i<end;++i)block["bytes"][decimal(pools[p].first+i-pools[p].second)]=data[i];}
        for(auto name:model_.pairs)block["registers"][name]=b->snapshot.file.findRegister(name)->reg->value;
        out["blocks"].push_back(std::move(block));}
    return out;
}
void Execution::adoptBudget(std::shared_ptr<StateBudget> budget){
    if(!budget->reserve(charged_))throw std::logic_error("validated execution reservation unavailable");
    budget_->release(charged_);budget_=std::move(budget);
}
void Execution::restore(const Json& j,bool newBranch,bool verifyCanonical){
    const bool legacy=j.at("schemaVersion")== (model_.id=="gameboy"?1:2);
    if((!legacy&&j.at("schemaVersion")!=3)||j.value("core",std::string("gameboy"))!=model_.id)throw std::invalid_argument("unsupported execution schema/core");
    if(!legacy&&counter(j.at("ramCapacity"))!=core_->ramCapacity())throw std::invalid_argument("execution physical RAM layout mismatch");
    auto requested=j.at("requestedMode").get<std::string>();if(requested!="baseline"&&requested!="snapshot")throw std::invalid_argument("invalid execution mode");
    if(!j.at("blocks").is_array()||!j.at("values").is_object()||!j.at("registers").is_array()||j["registers"].size()!=model_.lanes.size())throw std::invalid_argument("invalid execution collections");
    auto count=j["blocks"].size();if(count>122)throw std::invalid_argument("execution budget exceeded");
    auto parseCell=[&](const Json& value,unsigned max){Cell c;c.sequence=counter(value.at("sequence"));c.block=counter(value.at("block"));c.known=value.at("known").get<bool>();c.epoch=counter(value.at("epoch"));
        auto n=value.at("value");if(!n.is_number_integer()||n<0||n>max||c.block>count)throw std::invalid_argument("invalid execution supplier");c.value=n.get<uint16_t>();return c;};
    // This method is called on a disposable controller first by the coordinator.
    mode(requested);reason_=j.at("pauseReason").get<std::string>();epoch_=j.at("epoch").get<std::string>();epochId_=counter(j.at("epochCounter"));
    visits_=counter(j.at("visits"));sequence_=counter(j.at("sequence"));
    snapshotInstructions_=counter(j.at("snapshotInstructions"));baselineInstructions_=counter(j.at("baselineInstructions"));boundaries_=counter(j.at("authoritativeBoundarySteps"));snapshotReads_=counter(j.at("snapshotReads"));busReads_=counter(j.at("busReads"));
    if(epoch_!=decimal(epochId_)&&!(epoch_.empty()&&epochId_==0))throw std::invalid_argument("execution epoch mismatch");
    const auto savedCharge=legacy?3u*1024u*1024u+count*512u*1024u:baseReservation()+count*blockCharge;
    if(j.at("activeMode")!=(reason_.empty()?requested:"paused")||counter(j.at("executionBytes"))!=(snapshot_?savedCharge:0))throw std::invalid_argument("execution status mismatch");
    if(!snapshot_&&(!j["blocks"].empty()||!j["values"].empty()))throw std::invalid_argument("baseline checkpoint contains execution snapshots");
    for(size_t i=0;i<count;++i){if(!budget_->reserve(blockCharge))throw std::invalid_argument("execution restore exceeds budget");charged_+=blockCharge;
        auto b=std::make_unique<Block>(model_);auto& saved=j["blocks"][i];b->id=counter(saved.at("id"));b->visit=counter(saved.at("visit"));if(b->id!=i+1||b->visit>visits_)throw std::invalid_argument("invalid execution block ID/visit");
        if(!saved.at("instructions").is_array()||saved["instructions"].empty()||saved["instructions"].size()>2048||!saved.at("bytes").is_object()||!saved.at("registers").is_object())throw std::invalid_argument("invalid execution block");
        for(auto& entry:saved["instructions"]){auto key=parseKey(entry,legacy,verifyCanonical);if(owners_.contains(key))throw std::invalid_argument("duplicate execution ownership");owners_[key]={b->id,b->instructions.size()};b->instructions.push_back(key);}
        for(auto it=saved["bytes"].begin();it!=saved["bytes"].end();++it){auto a=counter(Json(it.key()));if(a>65535||!snapshotAddress(a)||!it.value().is_number_integer()||it.value()<0||it.value()>255)throw std::invalid_argument("invalid snapshot byte");uint8_t v=it.value().get<uint8_t>();b->snapshot.mem.write(std::span(&v,1),a);}
        for(size_t i=0;i<model_.pairs.size();++i){auto name=model_.pairs[i];auto v=saved["registers"].at(name);if(!v.is_number_integer()||v<0||v>model_.registerMaximum(i))throw std::invalid_argument("invalid snapshot register");b->snapshot.file.findRegister(name)->reg->value=v.get<uint16_t>();}
        blocks_.push_back(std::move(b));}
    for(auto it=j["values"].begin();it!=j["values"].end();++it){auto a=counter(Json(it.key()));uint8_t value=0;
        if(a>=values_.size()||(legacy&&a>65535)||core_->physicalRamIndex(core_->physicalRamLocation(a))!=a||
            (verifyCanonical&&!core_->physicalRamRead(a,value)))throw std::invalid_argument("invalid supplier address");
        auto c=parseCell(it.value(),255);
        if(!c.known||(verifyCanonical&&value!=c.value)||c.sequence>sequence_)throw std::invalid_argument("supplier differs from restored machine");
        values_[a]=c;}
    for(size_t i=0;i<model_.lanes.size();++i){registers_[i]=parseCell(j["registers"][i],model_.laneMaximum(i));if(registers_[i].known&&(registers_[i].sequence>sequence_||(verifyCanonical&&registers_[i].value!=lane(i,canonical_.file))))throw std::invalid_argument("register supplier differs from machine");}
    auto active=counter(j.at("activeBlock"));if(active>count)throw std::invalid_argument("invalid active block");active_=active?blocks_[active-1].get():nullptr;
    if(newBranch){epochId_=std::chrono::steady_clock::now().time_since_epoch().count();epoch_=decimal(epochId_);}
}
}
