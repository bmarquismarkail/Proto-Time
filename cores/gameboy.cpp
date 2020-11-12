#include "gameboy.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

    LR3592_DMG::LR3592_DMG()
    {
        file = buildRegisterfile();

        AF.registration(file, "AF");
        BC.registration(file, "BC");
        DE.registration(file, "DE");
        HL.registration(file, "HL");
        SP.registration(file, "SP");
        PC.registration(file, "PC");

        populateOpcodes();
    }

    BMMQ::RegisterFile<AddressType> LR3592_DMG::buildRegisterfile()
    {

        BMMQ::RegisterFile<AddressType> regfile;

        regfile.addRegister("AF", true);
        regfile.addRegister("BC", true);
        regfile.addRegister("DE", true);
        regfile.addRegister("HL", true);
        regfile.addRegister("SP", false);
        regfile.addRegister("PC", false);
        //regfile.addRegister // mar
        //regfile.addRegister // mdr

        return regfile;
    }
    //
    BMMQ::fetchBlock<AddressType, DataType> LR3592_DMG::fetch()
    {
        // building a static fetchblock for testing
        BMMQ::fetchBlock<AddressType, DataType> f ;
        f.setbaseAddress(cip);

        BMMQ::fetchBlockData<AddressType, DataType> data {0, std::vector<DataType> {0x3E} };

        f.getblockData().push_back(data);
        return f;
    };

    BMMQ::executionBlock<AddressType> LR3592_DMG::decode(BMMQ::OpcodeList &oplist, BMMQ::fetchBlock<AddressType, DataType>& fetchData)
    {
        // building a static execution block
        BMMQ::executionBlock<AddressType> b;
        mdr.value = 255;
        auto &fb = fetchData.getblockData();
        for( auto& i : fb ) {
            for (auto data : i.data)
                b.getBlock().push_back(opcodeList[data]);
        }
        return b;
    };

    void LR3592_DMG::execute(const BMMQ::executionBlock<AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb )
    {
        for (auto e : block.getBlock() ) {
            (e)(fb);
        }
    };
