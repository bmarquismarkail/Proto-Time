#include "WholeBlockFixtures.hpp"
#include "inst_cycle/research/MachineBlock.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
namespace {thread_local bool counting=false;thread_local std::size_t allocations=0;}
void* operator new(std::size_t n){if(counting)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace BMMQ::IR;
using namespace BMMQ::IR::Research;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class Adapter>void check(const CompiledBlock& artifact,Adapter adapter){
 auto owner=std::make_shared<OwnerIdentity>();auto binding=bindMachineBlock(artifact,adapter,owner,123,77);
 require(&binding.code().block()==&artifact.block(),"initial binding copied metadata");
 counting=true;
 for(unsigned i=0;i<10000;++i)binding=binding.remap(owner,123,i+100);
 counting=false;
 require(allocations==0,"scalar remap allocated");
 require(&binding.code().block()==&artifact.block(),"scalar remap changed artifact identity");
 require(binding.scalarGuardsMatch(10099,0,1),"live scalar mapping not updated");
 auto peek=[&](std::uint16_t address,std::uint8_t& byte){for(const auto& g:artifact.block().guards)if(g.kind==GuardKind::CodeBytes && address>=g.subject&&address-g.subject<g.bytes.size()){byte=g.bytes[address-g.subject];return true;}return false;};
 require(binding.guardsMatch(10099,0,1,peek),"unified live guard failed");
 require(!binding.guardsMatch(77,0,1,peek),"stale mapping accepted");
 require(!binding.guardsMatch(10099,~std::uint64_t{0},1,peek),"CPU boundary accepted");
 auto changed=[&](std::uint16_t a,std::uint8_t& b){bool r=peek(a,b);b^=1;return r;};
 require(!binding.guardsMatch(10099,0,1,changed),"changed bytes accepted");
 allocations=0;counting=true;auto copied=binding.remapCopied(owner,123,10100);counting=false;
 require(allocations>0 && &copied.code().block()!=&artifact.block(),"copy diagnostic control did not allocate");
 binding.fault();bool rejected=false;try{binding.remap(owner,123,10101);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"faulted remap accepted");allocations=0;
}
int main()try{check(emittedGameBoyBank(),GB::IRExecution::GameBoyCoreAdapter{});check(emittedGameGearBank(),BMMQ::GameGearIR::CoreAdapter{});std::cout<<"two cores: 10000 scalar remaps each, zero metadata allocations\n";}catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}
