#ifndef COMMON_MICROCODES
#define COMMON_MICROCODES

namespace BMMQ {
namespace CML {

template<typename T1, typename T2>
static void loadtmp(T1 *temp, T2 *reg)
{
    *temp = *reg;
}

template<typename T1, typename T2>
static void loadtmp(T1 *temp, T2 imm)
{
    loadtmp(temp, &imm);
}

template<typename T1>
static void setFlags(T1 *temp,  T1 value)
{
    loadtmp(temp, value);
}

template<typename T1>
static void inc(T1 *temp)
{
    *temp+=1;
}

template<typename T1>
static void dec(T1 *temp)
{
    *temp-=1;
}

template<typename T1>
static void rlc8(T1 *temp)
{
    *temp= ( *temp << 1 ) | (( *temp & 28 ) >> 7 );
}

template<typename T1, typename T2>
static void add(T1 *temp, T2 *value)
{
    *temp += *value;
}

template<typename T1, typename T2>
static void add(T1 *temp, T2 value)
{
    *temp += value;
}

template<typename T1, typename T2>
static void sub(T1 *temp, T2 *value)
{
    *temp -= *value;
}

template<typename T1, typename T2>
static void sub(T1 *temp, T2 value)
{
    add(temp, -value);
}

template<typename T1>
static void rrc8(T1 *temp)
{
    *temp= (*temp >> 1) | ( ( *temp & 1) <<7);
}

template<typename T1>
static void rl8(T1 *temp, bool carry)
{
    *temp= (*temp << 1) | carry;
}

template<typename T1>
static void rr8(T1 *temp, bool carry)
{
    *temp= ( *temp >> 1 ) | ( carry << 7 );
}

template<typename T1, typename T2>
static void jp(T1 *temp, T2 value, bool condition)
{
    if (condition) *temp = value;
}

template<typename T1, typename T2>
static void jr(T1 *temp, T2 value, bool condition)
{
    if (condition) *temp += value;
}

template<typename T1>
static void daa(T1 *temp)
{
    *temp= ( *temp / 10) + ( *temp % 10);
}

template<typename T1>
static void cpl(T1 *temp)
{
    *temp = ~ *temp;
}

template<typename T1, typename T2>
static void adc(T1 *temp, T2 value, bool carry)
{
    add(temp, value);
    *temp += carry;
}

template<typename T1, typename T2>
static void sbc(T1 *temp, T2 value, bool carry)
{
    sub(temp, value);
    *temp -= carry;
}

template<typename T1, typename T2>
static void iand(T1 *temp, T2 *reg)
{
    *temp &= *reg;
}

template<typename T1, typename T2>
static void iand(T1 *temp, T2 imm)
{
    iand(temp, imm);
}

template<typename T1, typename T2>
static void ixor(T1 *temp, T2 *reg)
{
    *temp ^= *reg;
}

template<typename T1, typename T2>
static void ixor(T1 *temp, T2 imm)
{
    ixor(temp, imm);
}

template<typename T1, typename T2>
static void ior(T1 *temp, T2 *reg)
{
    *temp |= *reg;
}

template<typename T1, typename T2>
static void ior(T1 *temp, T2 imm)
{
    ior(temp, imm);
}

template<typename T1, typename T2>
static void cmp(T1 *temp, T2 *reg)
{
    *temp = ( *temp == *reg);
}

template<typename T1, typename T2>
static void cmp(T1 *temp, T2 imm)
{
    cmp(temp, imm);
}

template<typename T1>
static void sla8(T1 *temp)
{
    *temp <<= 1;
}

template<typename T1>
static void sra8(T1 *temp)
{
    *temp >>= 1 | (*temp & 128);
}

template<typename T1>
static void swap8(T1 *temp)
{
    *temp = ( (*temp & 240) >> 4) | (*temp << 4);
}

template<typename T1>
static void srl8(T1 *temp)
{
    *temp >>= 1;
}

template<typename T1>
static void testbit8(T1 *temp, T1 *reg, unsigned char bitindex )
{
    *temp = ( *reg & ( 1 << bitindex ) );
}

template<typename T1>
static void setbit8(T1 *temp, unsigned char bitindex )
{
    *temp |= ( 1 << bitindex );
}

template<typename T1>
static void resetbit8(T1 *temp, unsigned char bitindex )
{
    *temp &= ~ ( 1 << bitindex );
}
}
}

#endif //COMMON_MICROCODES