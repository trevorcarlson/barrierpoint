#include "pin.H"
#include "isimpoint_inst.H"
#include "pinplay.H"
#include "instlib.H"

#include <vector>
#include <unordered_map>
#include <cassert>
using namespace INSTLIB;

#include "parse_func_xml.h"

#define KNOB_LOG_NAME  "log"
#define KNOB_REPLAY_NAME "replay"
#define KNOB_FAMILY "pintool:pinplay-driver"

struct thread_data {
   UINT64 icount;
};
thread_data tdata[256]; 

std::vector<UINT64> icount;

UINT64 region_count = 0;
BOOL in_roi = false;

BOOL control_log_enabled = false;

PINPLAY_ENGINE pinplay_engine;
funcData appFuncData;
std::unordered_map<int,bool> region_map;


KNOB_COMMENT pinplay_driver_knob_family(KNOB_FAMILY, "PinPlay Driver Knobs");

KNOB<BOOL>KnobReplayer(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                       KNOB_REPLAY_NAME, "0", "Replay a pinball");
KNOB<BOOL>KnobLogger(KNOB_MODE_WRITEONCE,  KNOB_FAMILY,
                     KNOB_LOG_NAME, "0", "Create a pinball");
KNOB<BOOL>KnobSaveICount(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                     "icount", "1", "Save the icount pintool control output file");
KNOB<std::string>KnobICountFile(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                     "icountfile", "pintool.in.csv", "Save the icount pintool control output to this file");
KNOB<BOOL>KnobUseROI(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                     "roi", "0", "Only save data during ROI");
KNOB<std::string>KnobRegionList(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                     "regionlist", "", "comma/space/colon separated list of regions");
KNOB<BOOL> KnobDebug(KNOB_MODE_WRITEONCE, "pintool", "debug", "0", "start debugger on internal exception");


LOCALFUN INT32 Usage(CHAR *prog)
{
    cerr << "Usage: " << prog << " Args  -- app appargs ..." << endl;
    cerr << "Arguments:" << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    
    return -1;
}

VOID saveICount(THREADID id, bool force = false)
{
   assert (id == 0);

   if (!in_roi && !force) {
      std::cout << "saveICount skipping at" << pinplay_engine.ReplayerGetICount(id) << std::endl;
      return;
   } else {
      std::cout << "saveICount saving at" << pinplay_engine.ReplayerGetICount(id) << std::endl;
   }

   icount.push_back(pinplay_engine.ReplayerGetICount(id));
}

#define SIM_CMD_ROI_START       1
#define SIM_CMD_ROI_END         2

ADDRINT handleMagic(ADDRINT gax, ADDRINT gbx, ADDRINT gcx, CONTEXT* ctxt, THREADID id)
{

   if (gax == SIM_CMD_ROI_START)
   {
      if (in_roi != false)
      {
         std::cerr << "Error: ROI_START seen, but we have already started." << std::endl;
         in_roi = false;
         PIN_RemoveInstrumentation();
      }
      in_roi = true;
      PIN_RemoveInstrumentation();
      if (KnobSaveICount)
      {
//         saveICount(id, true);
      }
   }
   else if (gax == SIM_CMD_ROI_END)
   {
      if (KnobSaveICount)
      {
         // Write with each GOMP_start and finally at ROI end
         saveICount(id, true);
      }

      in_roi = false;

      PIN_RemoveInstrumentation();
   }

   return 0;
}


VOID traceCallback(TRACE trace, VOID *v)
{

   BBL bbl_head = TRACE_BblHead(trace);
   for (BBL bbl = bbl_head; BBL_Valid(bbl); bbl = BBL_Next(bbl))
   {
      for(INS ins = BBL_InsHead(bbl); ins != BBL_InsTail(bbl) ; ins = INS_Next(ins))
      {
         if (INS_Disassemble(ins) == "xchg bx, bx")
         {
            if (KnobUseROI)
            {
               INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)handleMagic, IARG_REG_VALUE, REG_GAX, IARG_REG_VALUE, REG_GBX, IARG_REG_VALUE, REG_GCX, IARG_CONTEXT, IARG_THREAD_ID, IARG_RETURN_REGS, REG_GAX, IARG_END);
            }
         }
      }
   }


   for (BBL bbl = bbl_head; BBL_Valid(bbl); bbl = BBL_Next(bbl))
   {
      for(INS ins = BBL_InsHead(bbl); ins != BBL_InsTail(bbl) ; ins = INS_Next(ins))
      {
         if (KnobSaveICount)
         {
            if ((INS_Address(ins) == appFuncData.GOMP_parallel_start)) // || (INS_Address(ins) == appFuncData.exit))
            {
               INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)saveICount, IARG_THREAD_ID, IARG_BOOL, false, IARG_END);
            }

            // GOMP 4.0
            if ((appFuncData.GOMP_parallel != 0) && (INS_Address(ins) == appFuncData.GOMP_parallel))
            {
               INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)saveICount, IARG_THREAD_ID, IARG_BOOL, false, IARG_END);
            }
         }
      }
   }
}

VOID Fini(INT32 code, VOID *v)
{
   if (KnobSaveICount)
   {
      std::ofstream csv(KnobICountFile);
      int regionid = 0;
      UINT64 start_icount = 1;
      auto i = icount.begin();
      start_icount = *i;
      ++i;
      for ( ; i != icount.end() ; ++i)
      {
         UINT64 end_icount = *i;
         csv << "acomment-count-" << (end_icount-start_icount) << ",0," << regionid << "," << start_icount << "," << end_icount << ",1.0" << std::endl;
         regionid++;
         start_icount = end_icount; // Purposely overlap to prevent PinPlay relogging issues caused by regions that are too close but not overlapping
      }
      csv.close();
   }
   return;
}

int 
main(int argc, char *argv[])
{
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage(argv[0]);
    }

    pinplay_engine.Activate(argc, argv, KnobLogger, KnobReplayer);

    appFuncData = parseFuncData(pinplay_engine.ReplayerGetBaseName()+".procinfo.xml");
    if (appFuncData.main == 0x0 || appFuncData.exit == 0x0 || appFuncData.GOMP_parallel_start == 0x0)
    {
        std::cerr << "Application function pointer data is not valid" << std::endl;
        return 0;
    }

    if(!KnobUseROI)
    {
       in_roi = true;
    }

    TRACE_AddInstrumentFunction(traceCallback, 0);

    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
}
