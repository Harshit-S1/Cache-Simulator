#include "pin.H"
#include <iostream>
#include <fstream>

// global variables for file output, access counting, and memory boundary tracking
std::ofstream TraceFile;
UINT64 totalAccesses = 0;
ADDRINT mainLow = 0, mainHigh = 0;

// a command-line argument (KNOB) to limit the maximum memory accesses
// A default value of 0 indicates unlimited accesses allowed
KNOB<UINT64> KnobMaxAccesses(KNOB_MODE_WRITEONCE, "pintool",
    "max_accesses", "0", "Maximum number of memory accesses to trace (0 = unlimited)");

// helper function to safely terminate the application once the access limit is reached
inline VOID CheckLimit() {
    UINT64 limit = KnobMaxAccesses.Value();
    if (limit > 0 && totalAccesses >= limit) {
        PIN_ExitApplication(0); 
    }
}

// instruction fetches logging, also ensures they occur within the main executable's memory bounds through mainLow and mainHigh
VOID RecordInstFetch(VOID * ip) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "I " << ip << "\n"; // Logged as 'I'
        totalAccesses++;
    }
}

// memory read operations also logged within the main executable bounds
VOID RecordMemRead(VOID * ip, VOID * addr) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "R " << addr << "\n";
        totalAccesses++;
    }
}

// memory write operations also logged within the main executable bounds
VOID RecordMemWrite(VOID * ip, VOID * addr) {
    CheckLimit();
    if((ADDRINT)ip >= mainLow && (ADDRINT)ip <= mainHigh){
        TraceFile << "W " << addr << "\n"; 
        totalAccesses++;
    }
}

// Instrumentation block triggered for every instruction in the executable file
VOID Instruction(INS ins, VOID *v) {
    // 1. Instruction Fetches instrumented (Every executed instruction is fetched)
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordInstFetch, IARG_INST_PTR, IARG_END);

    // 2. Data Reads instrumented (Includes standard reads and secondary reads)
    if (INS_IsMemoryRead(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    }
    if (INS_HasMemoryRead2(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_INST_PTR, IARG_MEMORYREAD2_EA, IARG_END);
    }
    
    // 3. Data Writes instrumented
    if (INS_IsMemoryWrite(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite, IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_END);
    }
}

// Instrumentation function triggered when a new image (executable or shared library) is loaded
VOID Image(IMG img, VOID *v) {
    // helps to filter out shared libraries by capturing the memory bounds of the main executable only
    if (IMG_IsMainExecutable(img)) {
        mainLow = IMG_LowAddress(img);
        mainHigh = IMG_HighAddress(img);
    }
}

// Teardown function called upon application exit to safely close the trace file
VOID Fini(INT32 code, VOID *v) {
    TraceFile.close();
}

int main(int argc, char *argv[]) {
    // Initialize the PIN engine and parse command-line KNOB arguments
    if (PIN_Init(argc, argv)) {
        std::cerr << "Initialization failed. Ensure PIN is configured correctly." << std::endl;
        std::cerr << "Usage: pin -t MyPinTool.so -max_accesses <limit> -- <executable>" << std::endl;
        return -1;
    }

    // Open the output file for the memory trace
    TraceFile.open("memory_trace.out");
    
    // Register the instrumentation callbacks
    IMG_AddInstrumentFunction(Image, 0);       // Called when images are loaded
    INS_AddInstrumentFunction(Instruction, 0); // Called for each instruction
    PIN_AddFiniFunction(Fini, 0);              // Called at exit

    // Begin the instrumented execution of the target program
    PIN_StartProgram();
    
    return 0;
}