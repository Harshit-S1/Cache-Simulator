#include "pin.H"
#include <iostream>
#include <fstream>

std::ofstream TraceFile;
UINT64 totalAccesses = 0;
ADDRINT mainLow = 0, mainHigh = 0;

// Define a KNOB for maximum accesses (default is 0, meaning unlimited)
KNOB<UINT64> KnobMaxAccesses(KNOB_MODE_WRITEONCE, "pintool",
    "max_accesses", "0", "Maximum number of memory accesses to trace (0 = unlimited)");

// Helper function to check limits
inline VOID CheckLimit() {
    UINT64 limit = KnobMaxAccesses.Value();
    if (limit > 0 && totalAccesses >= limit) {
        PIN_ExitApplication(0); 
    }
}

// Track Instruction Fetches
VOID RecordInstFetch(VOID * ip) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "I " << ip << "\n"; // Logged as 'I'
        totalAccesses++;
    }
}

// Track Memory Reads
VOID RecordMemRead(VOID * ip, VOID * addr) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "R " << addr << "\n";
        totalAccesses++;
    }
}

// Track Memory Writes
VOID RecordMemWrite(VOID * ip, VOID * addr) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "W " << addr << "\n"; 
        totalAccesses++;
    }
}

VOID Instruction(INS ins, VOID *v) {
    // 1. Instrument Instruction Fetches (Every instruction executed is fetched)
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordInstFetch, IARG_INST_PTR, IARG_END);

    // 2. Instrument Data Reads
    if (INS_IsMemoryRead(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    }
    if (INS_HasMemoryRead2(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYREAD2_EA, IARG_END);
    }
    
    // 3. Instrument Data Writes
    if (INS_IsMemoryWrite(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite, IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_END);
    }
}

VOID Image(IMG img, VOID *v) {
    if (IMG_IsMainExecutable(img)) {
        mainLow = IMG_LowAddress(img);
        mainHigh = IMG_HighAddress(img);
    }
}

VOID Fini(INT32 code, VOID *v) {
    TraceFile.close();
}

int main(int argc, char *argv[]) {
    // Initialize PIN and allow KNOB arguments
    if (PIN_Init(argc, argv)) {
        std::cerr << "Initialization failed. Ensure PIN is configured correctly." << std::endl;
        std::cerr << "Usage: pin -t MyPinTool.so -max_accesses <limit> -- <executable>" << std::endl;
        return -1;
    }

    TraceFile.open("memory_trace.out");
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    
    return 0;
}