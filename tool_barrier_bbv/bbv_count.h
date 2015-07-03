#ifndef BBV_COUNT_H
#define BBV_COUNT_H

#include "fixed_types.h"
#include "fixed_point.h"

#include <vector>

class Bbv;

// Store (and construct) running instruction and BBV counts
class BbvCount
{
   private:
      UInt64 m_instrs;
      std::vector<UInt64> m_bbv_counts;

   public:
      // Number of dimensions to project BBVs to
      // Make sure this remains a multiple of four, or update the unrolled loop in BbvCount::count
      static const int NUM_BBV = 16; //1024; //16;

      BbvCount();

      void count(UInt64 address, UInt64 count);
      void add(UInt64 instrs, Bbv &bbv);
      void clear();
      int size() { return m_bbv_counts.size(); }

      UInt64 getDimension(int dim) const { return m_bbv_counts.at(dim); }
      UInt64 getInstructionCount(void) const { return m_instrs; }
      const std::vector<UInt64> &getBbv(void) const { return m_bbv_counts; }
};

class Bbv
{
   private:
      std::vector<SInt64> m_bbv;
   public:
      Bbv();
      // Construct per-region BBV from running counts at region start and end
      Bbv(const BbvCount &, const BbvCount &);

      void clear(void);
      SInt64 getDimension(int dim) const { return m_bbv.at(dim); }
      // Compute difference between two (per-region) BBVs
      Bbv operator- (const Bbv &) const;
      UInt64 length(void) const;

      friend Bbv operator+ (const Bbv &bbv1, const Bbv &bbv2);
      friend Bbv operator* (const FixedPoint &a, const Bbv &bbv);
};

#endif // BBV_COUNT_H
