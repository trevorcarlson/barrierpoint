#include "pin.H"

#include <deque>
#include <unordered_map>
#include <map>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdio.h>
#include <limits>
#include <cassert>
#include <iomanip>

#include "treap.h"
#include "parse_func_xml.h"

#define MAX_THREADS 64
#define DEFAULT_INSN_COUNT 10

#define CACHE_LINE_SIZE_BYTES 64 // 64KiB
#define CACHE_LINE_MASK (~(CACHE_LINE_SIZE_BYTES-1ULL)) // 0xffff..00
#define CACHE_SIZE_BYTES (8 * 1024 * 1024) // 8MiB
#define NUM_LINES_IN_CACHE (CACHE_SIZE_BYTES/CACHE_LINE_SIZE_BYTES)

#include "pinplay.H"
PINPLAY_ENGINE pinplay_engine;
KNOB<BOOL> KnobPinPlayLogger(KNOB_MODE_WRITEONCE,
                      "pintool", "log", "0",
                      "Activate the pinplay logger");
KNOB<BOOL> KnobPinPlayReplayer(KNOB_MODE_WRITEONCE,
                      "pintool", "replay", "0",
                      "Activate the pinplay replayer");

bool in_roi = false;
bool in_post_roi = false;
const bool debug = false;

// FIXME: Add read/write state
// FIXME: support multiple levels of hierarchy
// FIXME: Multiple processes means that we need to capture physical addresses instead of virtual ones
typedef struct
{
  rd rdist;
  unsigned int seedp; // for rand_r()
  std::vector<uint64_t> reuse_distance_hist;
  std::vector<std::vector<uint64_t>> reuse_distance_hists;
  uint32_t ompcallsseen;
//  bool cleared_first_run;
  std::vector<uint64_t> roi_insn_counts;
} thread_data_t;
thread_data_t td[MAX_THREADS];

typedef struct
{
   volatile UINT64 counter;
   int _dummy[15];
} atomic_t;
atomic_t resource_counter[MAX_THREADS] __attribute__((aligned(0x40))) = {{0}};

atomic_t max_thread_id = {0};

funcData appFuncData;

static inline UINT64 atomic_add_return(UINT64 i, atomic_t *v)
{
   return __sync_fetch_and_add(&(v->counter), i);
}
#define atomic_inc_return(v)  (atomic_add_return(1, v))
static inline UINT64 atomic_set(atomic_t *v, UINT64 i)
{
   return __sync_lock_test_and_set(&(v->counter), i);
}
static inline UINT64 atomic_get(atomic_t *v)
{
   return v->counter;
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "buffer.out", "output file");
KNOB<INT64> KnobBarrierCount(KNOB_MODE_WRITEONCE, "pintool", "b", decstr(-1), "Barrier count (default = all (-1))");
KNOB<INT64> KnobAppNum(KNOB_MODE_WRITEONCE, "pintool", "a", decstr(0), "Application number (default = 0)");
KNOB<BOOL> KnobEnableROI(KNOB_MODE_WRITEONCE, "pintool", "roi", "1", "Use ROI");

#define SIM_CMD_ROI_START       1
#define SIM_CMD_ROI_END         2

VOID handleROIEnd();

ADDRINT handleMagic(ADDRINT gax, ADDRINT gbx, ADDRINT gcx)
{
   if (gax == SIM_CMD_ROI_START)
   {
      if (in_roi != false)
      {
         std::cerr << "Error: ROI_START seen, but we have already started." << std::endl;
      }
      std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] Entering ROI" << std::endl;
      in_roi = true;
   }
   else if (gax == SIM_CMD_ROI_END)
   {
      std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] Leaving ROI" << std::endl;
      handleROIEnd();
      in_roi = false;
      in_post_roi = true;
   }

   return 0;
}

// Print a memory read record
VOID RecordMemRead(THREADID thread_id, VOID * ip, VOID * addr)
{
    // End if we have made it to the requested barrier count
    if ((KnobBarrierCount.Value() != -1) && (td[thread_id].ompcallsseen > KnobBarrierCount.Value()))
    {
        return;
    }
    if (in_post_roi)
    {
        return;
    }

/*
    if (in_roi && !td[thread_id].cleared_first_run)
    {
        td[thread_id].reuse_distance_hists.emplace_back(std::move(td[thread_id].reuse_distance_hist));
        td[thread_id].cleared_first_run = true;
    }
*/

    //printf("%p: R %p\n", ip, addr);
    UINT64 address = reinterpret_cast<UINT64>(addr) & CACHE_LINE_MASK;

    thread_data_t &t = td[thread_id];
    uint64_t mem_access_diff = t.rdist.reference(reinterpret_cast<const void* const>(address), 0, &t.seedp).first;
    if (mem_access_diff != static_cast<uint64_t>(-1))
    {
       uint64_t log2_rd =  mem_access_diff != 0 ? ( (sizeof(unsigned long long)*8-1) - __builtin_clzll(mem_access_diff) ) : 0; // Use the leading zero count
       if (log2_rd >= t.reuse_distance_hist.size())
       {
          t.reuse_distance_hist.resize(log2_rd+1);
       }
       t.reuse_distance_hist[log2_rd]++;
    }
}

// Print a memory write record
VOID RecordMemWrite(THREADID thread_id, VOID * ip, VOID * addr)
{
    //printf("%p: W %p\n", ip, addr);
    RecordMemRead(thread_id, ip, addr);
}

VOID applicationOMPCallback(THREADID threadIndex)
{
    if (!in_roi)
    {
        return;
    }

    if ((KnobBarrierCount.Value() == -1) || (td[threadIndex].ompcallsseen == KnobBarrierCount.Value()))
    {
        //writeWarmupTrace(threadIndex);
    }

    td[threadIndex].ompcallsseen++;

    td[threadIndex].reuse_distance_hists.emplace_back(std::move(td[threadIndex].reuse_distance_hist));
    assert(td[threadIndex].reuse_distance_hist.size() == 0); // does emplace_back work?

    // Create a new file if we are writing many barrier warmup files
    if (KnobBarrierCount.Value() == -1)
    {
        //createNewWarmupFile(threadIndex, td[threadIndex].ompcallsseen);
    }

    if (debug && (td[threadIndex].ompcallsseen == KnobBarrierCount.Value()))
    {
        std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] " << threadIndex << ": Seen " << td[threadIndex].ompcallsseen << " OMP calls" << std::endl;
    }
}

VOID multiApplicationOMPCallback(THREADID threadIndex)
{
    td[threadIndex].roi_insn_counts.push_back(pinplay_engine.ReplayerGetICount(threadIndex));

    for (unsigned int i = 0 ; i < MAX_THREADS ; i++)
    {
        applicationOMPCallback(i);
    }	
}

VOID handleROIEnd()
{
    // Record from GOMP_Start to each GOMP_Start, and finally end with ROI-end
    multiApplicationOMPCallback(0);
}

VOID routineStartCallback(RTN rtn, INS ins, TRACE trace)
{
   std::string rtn_name = RTN_Name(rtn).c_str();
   // GCC's OMP function start (per thread call)
   if (rtn_name.find(".omp_fn.") != string::npos)
   {
      INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(applicationOMPCallback), IARG_THREAD_ID, IARG_END);
   }
}

VOID traceCallback(TRACE trace, VOID *v)
{
   BBL bbl_head = TRACE_BblHead(trace);

   // Routine replacement
   RTN rtn = TRACE_Rtn(trace);
   if (RTN_Valid(rtn)
      && RTN_Address(rtn) == TRACE_Address(trace))
   {
      INS ins_head = BBL_InsHead(bbl_head);
      routineStartCallback(rtn, ins_head, trace);
   }

   for (BBL bbl = bbl_head; BBL_Valid(bbl); bbl = BBL_Next(bbl))
   {
      for(INS ins = BBL_InsHead(bbl); INS_Valid(ins) ; ins = INS_Next(ins))
      {
         if (KnobEnableROI)
         {
            if (INS_Disassemble(ins) == "xchg bx, bx")
            {
               INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)handleMagic, IARG_REG_VALUE, REG_GAX, IARG_REG_VALUE, REG_GBX, IARG_REG_VALUE, REG_GCX, IARG_RETURN_REGS, REG_GAX, IARG_END);
            }
         }

         if (INS_Address(ins) == appFuncData.GOMP_parallel_start)
         {
            INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)multiApplicationOMPCallback, IARG_THREAD_ID, IARG_END);
         }

         // GOMP 4.0
         if ((appFuncData.GOMP_parallel != 0) && (INS_Address(ins) == appFuncData.GOMP_parallel))
         {
            INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)multiApplicationOMPCallback, IARG_THREAD_ID, IARG_END);
         }


         // From the Pin manual
         UINT32 memOperands = INS_MemoryOperandCount(ins);

         // Iterate over each memory operand of the instruction.
         for (UINT32 memOp = 0; memOp < memOperands; memOp++)
         {
            if (INS_MemoryOperandIsRead(ins, memOp))
            {
               INS_InsertPredicatedCall(
                  ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                  IARG_THREAD_ID,
                  IARG_INST_PTR,
                  IARG_MEMORYOP_EA, memOp,
                  IARG_END);
            }
            if (INS_MemoryOperandIsWritten(ins, memOp))
            {
               INS_InsertPredicatedCall(
                  ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                  IARG_THREAD_ID,
                  IARG_INST_PTR,
                  IARG_MEMORYOP_EA, memOp,
                  IARG_END);
            }
         }
      }
   }
}

VOID threadStart(THREADID thread_id, CONTEXT *ctxt, INT32 flags, VOID *v)
{
   if (thread_id >= MAX_THREADS)
   {
      std::cerr << "Error: More threads requested than we have allocated space for (MAX=" << MAX_THREADS << ", id=" << thread_id << ")" << std::endl;
      PIN_RemoveInstrumentation();
   }

   // Grab our start time from an earlier thread
   if (thread_id > 0)
   {
      // Not perfect, but close enough
      while (atomic_set(&max_thread_id, thread_id) < thread_id) {}
   }
}

VOID programStart(VOID *v)
{
   std::cerr << "[TOOL_BARRIER_REUSE_DISTANCE] Starting Trace" << std::endl;

   for (int i = 0 ; i < MAX_THREADS ; i++)
   {
      td[i].ompcallsseen = 0;
//      td[i].cleared_first_run = false;
      td[i].seedp = i;
   }
}

VOID programEnd(INT32, VOID *v)
{
   std::ofstream fp(KnobOutputFile.Value());
   // Write out all of the data here
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      int barrier_count = 0;
      for (auto &hist : td[i].reuse_distance_hists)
      {
         fp << "Th:" << std::setw(3) << i << " b:" << std::setw(4) << barrier_count;
         for (auto &ent : hist)
         {
            fp << " " << std::setw(10) << ent;
         }
         barrier_count ++;
         fp << std::endl;
      }
   }
   fp.close();

   size_t max_size = 0;
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      for (auto &hist : td[i].reuse_distance_hists)
      {
         max_size = std::max(max_size, hist.size());
      }
   }

   std::ofstream fp_bb(KnobOutputFile.Value()+".bb");
   for (size_t h = 0 ; h < td[0].reuse_distance_hists.size() ; h++ )
   {
      fp_bb << (h == 0 ? "W" : "T");
      for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
      {
         auto &hist = td[i].reuse_distance_hists[h];
         int entry_count = 0;
         for (auto &ent : hist)
         {
            if (ent != 0)
            {
               fp_bb << ":" << 1+entry_count+(i*max_size) << ":" << ent << " ";
            }
            entry_count ++;
         }
      }
      fp_bb << std::endl;
   }
   fp_bb.close();

   int status = system(("gzip -f "+KnobOutputFile.Value()+".bb").c_str());
   if (status == -1 || WEXITSTATUS(status))
   {
      std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] Warning: gzip exited with code " << WEXITSTATUS(status) << std::endl;
   }


   unsigned int maxsize = 0;
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      maxsize = std::max<unsigned int>(maxsize, td[i].roi_insn_counts.size());
   }
   std::ofstream fp_ppinsn(KnobOutputFile.Value()+"_pinplayinsncount");
   for (unsigned int b = 0 ; b < maxsize ; b++)
   {
      uint64_t insncount = (td[0].roi_insn_counts.size() <= b) ? 0 : td[0].roi_insn_counts[b];
      fp_ppinsn << insncount;
      //std::cout << "[BARRIER_BBV:0] Count = " << insncount << std::endl;
      for (uint64_t i = 1 ; i <= atomic_get(&max_thread_id) ; i++)
      {  
         if (td[i].roi_insn_counts.size() <= b)
         { 
            fp_ppinsn << "," << 0;
         }
         else
         { 
            fp_ppinsn << "," << td[i].roi_insn_counts[b];
         }
      }
      fp_ppinsn << "\n";
   }
   fp_ppinsn.close();
   int exitstatus = system((std::string("gzip ")+KnobOutputFile.Value()+"_pinplayinsncount").c_str());
   if (WEXITSTATUS(exitstatus) != 0)
      std::cerr << "ERROR: gzip exit status of " << exitstatus << std::endl;

}

VOID programDetach(VOID *v)
{
   programEnd(0, v);
}

int main(int argc, char * argv[])
{
   PIN_InitSymbols();
   if (PIN_Init(argc, argv))
   {
      std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] ERROR: Unable to parse the command-line options." << std::endl;
   }

   pinplay_engine.Activate(argc, argv,
      KnobPinPlayLogger, KnobPinPlayReplayer);

   appFuncData = parseFuncData(pinplay_engine.ReplayerGetBaseName()+".procinfo.xml");
   if (appFuncData.main == 0x0 || appFuncData.exit == 0x0 || appFuncData.GOMP_parallel_start == 0x0)
   {
      std::cerr << "Application function pointer data is not valid" << std::endl;
      return 0;
   }

   std::cout << "[TOOL_BARRIER_REUSE_DISTANCE] Barrier=" << KnobBarrierCount.Value() << ((KnobBarrierCount.Value() == -1) ? "(all)" : "") << " AppNum=" << KnobAppNum.Value() << " Output prefix=[" << KnobOutputFile.Value() << "]" << std::endl;

   PIN_AddApplicationStartFunction(programStart, 0);
   PIN_AddFiniFunction(programEnd, 0);
   PIN_AddDetachFunction(programDetach, 0);

   PIN_AddThreadStartFunction(threadStart, 0);

   TRACE_AddInstrumentFunction(traceCallback, 0);

   if (!KnobEnableROI)
   {
      in_roi = true;
   }

   PIN_StartProgram();
}
