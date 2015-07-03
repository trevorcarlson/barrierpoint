#include "bbv_count.h"

#include <algorithm>

// RNG parameters, defaults taken from drand48
#define RNG_A 0x5DEECE66D
#define RNG_C 0xB
#define RNG_M ((1L << 48) - 1)

// Same as drand48, but inlined for efficiency
inline UInt64 rng_next(UInt64 &state)
{
   state = (RNG_A * state + RNG_C) & RNG_M;
   return state >> 16;
}
inline UInt64 rng_seed(UInt64 seed)
{
   return (seed << 16) + 0x330E;
}


BbvCount::BbvCount()
   : m_instrs(0)
   , m_bbv_counts(NUM_BBV, 0)
{
}

void
BbvCount::count(UInt64 address, UInt64 count)
{
   m_instrs += count;

   // Perform random projection of basic-block vectors onto NUM_BBV dimensions
   // As there are too many BBVs, we cannot store the projection matrix, rather,
   // we re-generate it on request using an RNG seeded with the BBV address.
   // Since this is a hot loop in FAST_FORWARD mode, use an inlined RNG
   // and four parallel code paths to exploit as much ILP as possible.
   UInt64 s0 = rng_seed(address), s1 = rng_seed(address + 1), s2 = rng_seed(address + 2), s3 = rng_seed(address + 3);
   for(int i = 0; i < NUM_BBV; i += 4)
   {
      UInt64 weight = rng_next(s0);
      m_bbv_counts[i] += (weight & 0xffff) * count;
      weight = rng_next(s1);
      m_bbv_counts[i+1] += (weight & 0xffff) * count;
      weight = rng_next(s2);
      m_bbv_counts[i+2] += (weight & 0xffff) * count;
      weight = rng_next(s3);
      m_bbv_counts[i+3] += (weight & 0xffff) * count;
   }
}

void
BbvCount::add(UInt64 instrs, Bbv &bbv)
{
   m_instrs += instrs;
   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      m_bbv_counts[i] += instrs * bbv.getDimension(i);
   }
}

void
BbvCount::clear()
{
   m_instrs = 0;
   m_bbv_counts.clear();
   m_bbv_counts.resize(BbvCount::NUM_BBV);
}


Bbv::Bbv ()
   : m_bbv(BbvCount::NUM_BBV, 0)
{}

Bbv::Bbv (const BbvCount &bbv1, const BbvCount &bbv2)
   : m_bbv(BbvCount::NUM_BBV, 0)
{
   UInt64 instrs = bbv2.getInstructionCount() - bbv1.getInstructionCount();
   if (instrs == 0)
      instrs = 1;
   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      m_bbv[i] = (static_cast<SInt64>(bbv2.getDimension(i)) - bbv1.getDimension(i)) / instrs;
   }
}

void
Bbv::clear(void)
{
   m_bbv.clear();
   m_bbv.resize(BbvCount::NUM_BBV, 0);
}

Bbv
Bbv::operator- (const Bbv &bbv) const
{
   Bbv out;

   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      out.m_bbv[i] = static_cast<SInt64>(m_bbv[i]) - bbv.m_bbv[i];
   }

   return out;
}

UInt64
Bbv::length(void) const
{
   UInt64 diff = 0;

   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      diff += std::abs(m_bbv[i]);
   }

   // Normalize by the number of BBVs to keep the results consistent as the count changes
   return diff / BbvCount::NUM_BBV;
}

Bbv
operator+ (const Bbv &bbv1, const Bbv &bbv2)
{
   Bbv out;

   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      out.m_bbv[i] = bbv1.m_bbv[i] + bbv2.m_bbv[i];
   }

   return out;
}

Bbv
operator* (const FixedPoint &a, const Bbv &bbv)
{
   Bbv out;

   for(int i = 0; i < BbvCount::NUM_BBV; ++i)
   {
      out.m_bbv[i] = FixedPoint::floor(a * bbv.m_bbv[i]);
   }

   return out;
}
