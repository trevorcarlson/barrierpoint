#include "pin.H"

#include "bbv_count.h"
//#include "zfstream.h"

#include <deque>
#include <unordered_map>
#include <map>
#include <iostream>
#include <iterator>
#include <stdio.h>
#include <limits>
#include <tuple>

#include "tuple_hash.h"
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
PIN_LOCK new_threadid_lock;

// FIXME: Add read/write state
// FIXME: support multiple levels of hierarchy
// FIXME: Multiple processes means that we need to capture physical addresses instead of virtual ones
typedef struct
{
  std::vector<BbvCount> bbv_counts;
  BbvCount bbv_count;
  std::vector<std::unordered_map<std::tuple<uint64_t,uint32_t>,uint32_t> > roi_bbvs;
  std::unordered_map<std::tuple<uint64_t,uint32_t>,uint32_t> roi_bbv;
  uint32_t ompcallsseen;
  uint32_t writecalls;
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
      std::cout << "[TOOL_BARRIER_BBV] Entering ROI" << std::endl;
      PIN_GetLock(&new_threadid_lock, 0);
      in_roi = true;
      PIN_ReleaseLock(&new_threadid_lock);
   }
   else if (gax == SIM_CMD_ROI_END)
   {
      std::cout << "[TOOL_BARRIER_BBV] Leaving ROI" << std::endl;
      handleROIEnd();
      PIN_GetLock(&new_threadid_lock, 0);
      in_roi = false;
      in_post_roi = true;
      PIN_ReleaseLock(&new_threadid_lock);
   }

   return 0;
}

VOID createNewWarmupFile(THREADID thread_id, int barriercount)
{
}

VOID writeWarmupTrace(THREADID threadid)
{
   //GetLock(&new_threadid_lock, threadid);
   //td[threadid].writecalls++;
   //std::cout << "[BARRIER_BBV:" << threadid << "] Saving data " << (td[threadid].writecalls) << std::endl; 
   //ReleaseLock(&new_threadid_lock);

   td[threadid].bbv_counts.push_back(td[threadid].bbv_count);
   td[threadid].bbv_count.clear();

   td[threadid].roi_bbvs.emplace_back(td[threadid].roi_bbv);
   td[threadid].roi_bbv.clear();
}

VOID captureBBVs(THREADID thread_id, ADDRINT address, INT32 count)
{
   if (!in_roi)
   {
       return;
   }

   td[thread_id].bbv_count.count(address, count);

   td[thread_id].roi_bbv[std::tuple<uint64_t,uint32_t>(address, count)] += count;
}

VOID applicationOMPCallback(THREADID threadIndex)
{
    // FIXME, how can we have so many callbacks from the same thread? Doesn't make sense, right?
    //PIN_GetLock(&new_threadid_lock, threadIndex);
    //std::cout << "[BARRIER_BBV:" << threadIndex << "] Seen " << (td[threadIndex].ompcallsseen+1) << " OMP calls" << std::endl; 
    //PIN_ReleaseLock(&new_threadid_lock);

    if (!in_roi)
    {
        return;
    }

    if ((KnobBarrierCount.Value() == -1) || (td[threadIndex].ompcallsseen == KnobBarrierCount.Value()))
    {
        writeWarmupTrace(threadIndex);
    }

    td[threadIndex].ompcallsseen++;

    // Create a new file if we are writing many barrier warmup files
    if (KnobBarrierCount.Value() == -1)
    {
        createNewWarmupFile(threadIndex, td[threadIndex].ompcallsseen);
    }

    if (debug && (td[threadIndex].ompcallsseen == KnobBarrierCount.Value()))
    {
        std::cout << "[TOOL_BARRIER_BBV] " << threadIndex << ": Seen " << td[threadIndex].ompcallsseen << " OMP calls" << std::endl;
    }
}

VOID multiApplicationOMPCallback(THREADID threadIndex)
{
    std::cerr << "[TOOL_BBV] Getting icount for thread " << threadIndex << std::endl;
    std::cerr << "[TOOL_BBV] icount for thread " << threadIndex << " is " << pinplay_engine.ReplayerGetICount(threadIndex) << std::endl;
    std::cerr << "[TOOL_BBV] Pushing icount into roi_insn_counts" << std::endl;
    td[threadIndex].roi_insn_counts.push_back(pinplay_engine.ReplayerGetICount(threadIndex));
    std::cerr << "[TOOL_BBV] Done pushing icount into roi_insn_counts" << std::endl;

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
      // Instrument the BBVs
      INS_InsertCall(BBL_InsTail(bbl), IPOINT_BEFORE, (AFUNPTR)captureBBVs, IARG_THREAD_ID, IARG_ADDRINT, BBL_Address(bbl), IARG_UINT32, BBL_NumIns(bbl), IARG_END);

      if (KnobEnableROI)
      {
	 for(INS ins = BBL_InsHead(bbl); INS_Valid(ins) ; ins = INS_Next(ins))
	 {
	    if (INS_Disassemble(ins) == "xchg bx, bx")
	    {
	       INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)handleMagic, IARG_REG_VALUE, REG_GAX, IARG_REG_VALUE, REG_GBX, IARG_REG_VALUE, REG_GCX, IARG_RETURN_REGS, REG_GAX, IARG_END);
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
   std::cerr << "[TOOL_BARRIER_BBV] Starting Trace" << std::endl;

   for (int i = 0 ; i < MAX_THREADS ; i++)
   {
      td[i].ompcallsseen = 0;
   }
}
#include <fstream>
#include <sstream>
VOID programEnd(INT32, VOID *v)
{
/*
   // Write a final vector
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      writeWarmupTrace(i);
      //std::cout << "[BARRIER_BBV:" << i << "]" << td[i].bbv_counts.size() << " " << td[i].writecalls << " " << td[i].roi_bbvs.size() << std::endl;
   }
*/

   // Create a BBV map
   int bbvid = 1;
   std::unordered_map<std::tuple<uint64_t,uint32_t>, uint32_t> bbvids;
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      for (auto &v : td[i].roi_bbvs)
      {
         for (auto &m : v)
         {
            if (bbvids.count(m.first) == 0)
            {
               bbvids[m.first] = bbvid;
               bbvid++;
            }
         }
      }
   } 

   unsigned int maxsize = 0;
   for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
   {
      maxsize = std::max<unsigned int>(maxsize, td[i].roi_bbvs.size());
   }
   {
     std::ofstream fp(KnobOutputFile.Value()+"_count");
     int num_bbvids = bbvid-1;
     // Write out the standard BBVs
     for (unsigned int b = 0 ; b < maxsize ; b++)
     {
        fp << (b == 0 ? "W" : "T");
	for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
	{
           if (td[i].roi_bbvs.size() <= b)
           {
              continue;
           }
	   for (auto &m: td[i].roi_bbvs[b])
	   {
	      uint32_t bb = bbvids[m.first];
	      fp << ":" << (bb+(i*num_bbvids)) << ":" << m.second << " ";
	   }
	}
	fp << "\n";
     }
     fp.close();
     int exitstatus = system((std::string("gzip -f ")+KnobOutputFile.Value()+"_count").c_str());
     if (WEXITSTATUS(exitstatus) != 0)
	std::cerr << "ERROR: gzip exit status of " << exitstatus << std::endl;
   }

   std::ofstream fp(KnobOutputFile.Value());
   //vostream *fp = new vofstream(KnobOutputFile.Value().c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
   //if (true /*compress*/)
   //{
   //   fp = new ozstream(fp);
   //}

 
   for (unsigned int b = 0 ; b < td[0].bbv_counts.size() ; b++)
   {
      //fp->write("T", 1);
      fp << "T";
      for (uint64_t i = 0 ; i <= atomic_get(&max_thread_id) ; i++)
      {
         for (int64_t bb = 0 ; bb < td[0].bbv_counts[0].size() ; bb++)
         {
         //ostringstream o;
         //o << ":" << (1+b+(i*BbvCount::NUM_BBV)) << ":" << td[i].bbv_counts[b].getBbv()[bb] << " ";
         fp << ":" << (1+bb+(i*BbvCount::NUM_BBV)) << ":" << td[i].bbv_counts[b].getBbv()[bb] << " ";
         //fp->write(o.str().c_str(), o.str().length());
         }
      }
      //fp->write("\n", 1);
      fp << "\n";
   }

   fp.close();
   //fp->flush();
   //delete fp;
   //fp = NULL;
   int exitstatus = system((std::string("gzip ")+KnobOutputFile.Value()).c_str());
   if (WEXITSTATUS(exitstatus) != 0)
      std::cerr << "ERROR: gzip exit status of " << exitstatus << std::endl;


   std::ofstream fp_insn(KnobOutputFile.Value()+"_insncount");
   for (unsigned int b = 0 ; b < maxsize ; b++)
   {
      uint64_t insncount = (td[0].roi_bbvs.size() <= b) ? 0 : td[0].bbv_counts[b].getInstructionCount();
      fp_insn << insncount;
      //std::cout << "[BARRIER_BBV:0] Count = " << insncount << std::endl;
      for (uint64_t i = 1 ; i <= atomic_get(&max_thread_id) ; i++)
      {
         if (td[i].roi_bbvs.size() <= b)
         {
            fp_insn << "," << 0;
         }
         else
         {
            fp_insn << "," << td[i].bbv_counts[b].getInstructionCount();
         }
      }
      fp_insn << "\n";
   }
   fp_insn.close();
   exitstatus = system((std::string("gzip ")+KnobOutputFile.Value()+"_insncount").c_str());
   if (WEXITSTATUS(exitstatus) != 0)
      std::cerr << "ERROR: gzip exit status of " << exitstatus << std::endl;


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
   exitstatus = system((std::string("gzip ")+KnobOutputFile.Value()+"_pinplayinsncount").c_str());
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
      std::cout << "[TOOL_BARRIER_BBV] ERROR: Unable to parse the command-line options." << std::endl;
   }

   pinplay_engine.Activate(argc, argv,
      KnobPinPlayLogger, KnobPinPlayReplayer);

   appFuncData = parseFuncData(pinplay_engine.ReplayerGetBaseName()+".procinfo.xml");
   if (appFuncData.main == 0x0 || appFuncData.exit == 0x0 || appFuncData.GOMP_parallel_start == 0x0)
   {
      std::cerr << "Application function pointer data is not valid" << std::endl;
      return 0;
   }

   PIN_InitLock(&new_threadid_lock);

   std::cout << "[TOOL_BARRIER_BBV] Barrier=" << KnobBarrierCount.Value() << ((KnobBarrierCount.Value() == -1) ? "(all)" : "") << " AppNum=" << KnobAppNum.Value() << " Output prefix=[" << KnobOutputFile.Value() << "]" << std::endl;

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
