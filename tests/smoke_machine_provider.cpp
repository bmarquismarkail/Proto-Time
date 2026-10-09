#include "emulator/DynamicMachineProvider.hpp"
#include <dlfcn.h>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <unistd.h>
#include <source_location>
#include <thread>
namespace {
void check(bool value, std::source_location where=std::source_location::current()) { if(!value) throw std::runtime_error("dynamic provider check failed at line " + std::to_string(where.line())); }
template<class F> void rejected(F f) { bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught); }
void mode(const char* value) { if(value)setenv("TIME_TEST_PROVIDER_MODE",value,1);else unsetenv("TIME_TEST_PROVIDER_MODE"); }
}
int main(int argc,char** argv) {
    check(argc==2);const auto module=std::filesystem::absolute(argv[1]);
    for(const char* bad:{"version","count","size","callback","duplicate","unknown","string"}) {
        mode(bad); BMMQ::MachineRegistry registry;
        rejected([&]{BMMQ::registerDynamicMachineProviders(registry,module);});check(registry.descriptors().empty());
    }
    mode(nullptr);
    {
        BMMQ::MachineRegistry registry;BMMQ::registerDynamicMachineProviders(registry,module);
        check(registry.descriptors().size()==2);
        rejected([&]{BMMQ::registerDynamicMachineProviders(registry,module);});check(registry.descriptors().size()==2);
        void* observer=dlopen(module.c_str(),RTLD_NOW|RTLD_NOLOAD);check(observer);
        auto releases=reinterpret_cast<unsigned(*)()>(dlsym(observer,"time_test_provider_releases"));check(releases);
        auto before=releases();
        for(const char* bad:{"bad-handle","decline","cross-family","bad-bios"}) {
            mode(bad);rejected([&]{(void)registry.create("external-gb");});
        }
        check(releases()==before+4);mode(nullptr);dlclose(observer);
        // Registry metadata stays immutable while independent machines are made.
        std::vector<std::jthread> workers;
        for(int i=0;i<4;++i) workers.emplace_back([&registry] {
            for(const char* id:{"external-gb","external-gg"}) {
                auto owned=registry.create(id);owned->loadRom(std::vector<uint8_t>(32768));owned->step();
            }
        });
        workers.clear();
    }
    for(const auto& pair:{std::pair{"external-gb","gameboy"},std::pair{"external-gg","gamegear"}}) {
        std::unique_ptr<BMMQ::Machine> machine;
        {
            BMMQ::MachineRegistry registry;BMMQ::registerDynamicMachineProviders(registry,module);
            machine=registry.create(pair.first);
        }
        // A live instance pins the module after its registry/factory is destroyed.
        void* loaded=dlopen(module.c_str(),RTLD_NOW|RTLD_NOLOAD);check(loaded);dlclose(loaded);
        check(machine->visualTargetId()==pair.second && machine->supportsVisualPacks() && machine->supportsVisualCapture());
        std::vector<uint8_t> rom(32768);machine->loadRom(rom);
        auto baseline=BMMQ::MachineRegistry::builtins().create(pair.second);baseline->loadRom(rom);
        for(int i=0;i<32;++i) {machine->step();baseline->step();check(machine->readRegisterPair("PC")==baseline->readRegisterPair("PC"));}
        constexpr auto ram=0xc010;
        machine->runtimeContext().write8(ram,0x5a);check(machine->runtimeContext().peek8(ram)==0x5a);
        auto state=std::filesystem::temp_directory_path()/("time-provider-"+std::to_string(getpid())+".state");
        machine->save_state(state);machine->runtimeContext().write8(ram,0x11);machine->load_state(state);
        check(machine->runtimeContext().peek8(ram)==0x5a);std::filesystem::remove(state);
        machine.reset();check(!dlopen(module.c_str(),RTLD_NOW|RTLD_NOLOAD));
    }
    // A C factory supplies a real BIOS; this is more than a string alias.
    mode("bios");
    for(const auto& id:{"external-gb","external-gg"}) {
        BMMQ::MachineRegistry registry;BMMQ::registerDynamicMachineProviders(registry,module);
        auto machine=registry.create(id);machine->loadRom(std::vector<uint8_t>(32768));
        check(machine->runtimeContext().peek8(0)==0x3e);machine->step();check((machine->readRegisterPair("AF")>>8)==0x42);
        if (std::string_view(id)=="external-gb") {
            machine->runtimeContext().write8(0xff50,1);check(machine->runtimeContext().peek8(0)==0);
            machine->runtimeContext().write8(0xff50,0);check(machine->runtimeContext().peek8(0)==0);
        }
        machine->loadRom(std::vector<uint8_t>(32768));check(machine->runtimeContext().peek8(0)==0x3e);
    }
    mode(nullptr);std::cout<<"Both-core provider factories, BIOS, execution, state, ABI rejection, rollback and module lifetimes passed\n";
}
