#include "Core/UUID.h"
#include <random>

namespace Dark
{

static std::mt19937_64& rng() 
{
    static std::random_device rd;
    static std::mt19937_64 gen{rd()};
    return gen;
}

UUID::UUID() : m_id(rng()())
{}

UUID::UUID(uint64_t id) : m_id(id)
{}

} // namespace DE
