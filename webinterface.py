import streamlit as st
import subprocess
import os
import tempfile
import re
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go

st.set_page_config(page_title="Cache Simulator UI", layout="wide")
st.title("Multi-Level Cache Simulator & PinTool Profiler")

st.markdown("""
Upload a compiled C/C++ executable. The system will use **Intel PIN** to trace its memory accesses 
and run it through the **3-Level Inclusive Cache Simulator**.
""")

# --- STATE MANAGEMENT ---
if "trace_ready" not in st.session_state:
    st.session_state.trace_ready = False
if "last_filename" not in st.session_state:
    st.session_state.last_filename = None
if "last_max_accesses" not in st.session_state:
    st.session_state.last_max_accesses = None

# --- SIDEBAR CONFIGURATION ---
st.sidebar.header("Cache Specifications")

block_size = st.sidebar.selectbox("Block Size (Bytes)", [16, 32, 64, 128], index=2)

st.sidebar.subheader("L1 Instruction Cache (L1i)")
l1i_size = st.sidebar.number_input("L1i Size (Bytes)", min_value=1024, value=32768, step=1024)
l1i_assoc = st.sidebar.selectbox("L1i Associativity", [1, 2, 4, 8, 16], index=3)

st.sidebar.subheader("L1 Data Cache (L1d)")
l1d_size = st.sidebar.number_input("L1d Size (Bytes)", min_value=1024, value=49152, step=1024)
l1d_assoc = st.sidebar.selectbox("L1d Associativity", [1, 2, 4, 8, 16], index=4) 

st.sidebar.subheader("Advanced Microarchitecture")
wb_entries = st.sidebar.slider("Write Buffer Entries", min_value=0, max_value=64, value=8, step=4)
vc_entries = st.sidebar.slider("Victim Cache Entries", min_value=0, max_value=64, value=8, step=4)

st.sidebar.subheader("L2 Cache")
l2_size = st.sidebar.number_input("L2 Size (Bytes)", min_value=1024, value=262144, step=1024)
l2_assoc = st.sidebar.selectbox("L2 Associativity", [1, 2, 4, 8, 16], index=3)

st.sidebar.subheader("L3 Cache")
l3_size = st.sidebar.number_input("L3 Size (Bytes)", min_value=1024, value=2097152, step=1024)
l3_assoc = st.sidebar.selectbox("L3 Associativity", [1, 2, 4, 8, 16, 32], index=4)

st.sidebar.subheader("Policies")
replace_policy_map = {
    "LRU": 0, "LFU": 1, "FIFO": 2, "Random": 3, 
    "SRRIP": 4, "NRU": 5, "Tree-PLRU": 6, "Bélády (OPT)": 7
}
write_policy_map = {"Write-Through": 0, "Write-Back": 1}

rep_policy_str = st.sidebar.selectbox("Replacement Policy", list(replace_policy_map.keys()))
write_policy_str = st.sidebar.selectbox("Write Policy", list(write_policy_map.keys()))

rep_policy = replace_policy_map[rep_policy_str]
write_policy = write_policy_map[write_policy_str]

st.sidebar.subheader("Evaluation Options")
run_opt_benchmark = st.sidebar.checkbox("Compare against Bélády's Optimal (Theoretical Max)", value=False, 
                                        help="Runs the simulator twice to compare your selected policy against the mathematical ceiling. (Takes slightly longer).")

st.sidebar.subheader("Simulated Latencies (Cycles)")
l1_lat = st.sidebar.slider("L1 Latency", 1, 5, 2)
l2_lat = st.sidebar.slider("L2 Latency", 5, 20, 10)
l3_lat = st.sidebar.slider("L3 Latency", 20, 60, 40)
mem_lat = st.sidebar.slider("Main Memory Latency", 80, 300, 100)

st.sidebar.subheader("Simulation Limits")
max_accesses = st.sidebar.number_input(
    "Max Memory Accesses (0 = Unlimited)", 
    min_value=0, 
    value=500000, 
    step=100000,
    help="Limits the trace file size. Set to 0 to trace the entire program."
)

st.sidebar.subheader("System Paths")
PIN_EXECUTABLE = st.sidebar.text_input("PIN Executable Path", "/home/harshit/pin_kit/pin")
PIN_TOOL = st.sidebar.text_input("PIN Tool (.so) Path", "/home/harshit/pin_kit/source/tools/MyPinTool/obj-intel64/MyPinTool.so")
CACHE_SIMULATOR = st.sidebar.text_input("Simulator Executable Path", "./cacheSimulator")

uploaded_file = st.file_uploader("Upload your target executable file", type=None)

# --- HELPER FUNCTIONS ---
def extract_stat(pattern, text, is_float=False):
    match = re.search(pattern, text)
    if match:
        return float(match.group(1)) if is_float else int(match.group(1))
    return 0

def run_cache_simulator(target_policy_id):
    sim_cmd = [
        CACHE_SIMULATOR, 
        str(block_size),
        str(l1i_size), str(l1i_assoc),  
        str(l1d_size), str(l1d_assoc),  
        str(l2_size), str(l2_assoc),
        str(l3_size), str(l3_assoc),
        str(wb_entries), str(vc_entries), 
        str(target_policy_id), str(write_policy),
        "memory_trace.out"
    ]
    result = subprocess.run(sim_cmd, check=True, stdout=subprocess.PIPE, text=True)
    out = result.stdout
    
    return {
        "output": out,
        "l1i_hits": extract_stat(r"L1i Hits:\s+(\d+)", out),
        "l1i_miss": extract_stat(r"L1i Misses:\s+(\d+)", out),
        "l1i_rate": extract_stat(r"L1i Hit Rate:\s+([\d\.]+)", out, True),
        "l1d_hits": extract_stat(r"L1d Hits:\s+(\d+)", out),
        "l1d_miss": extract_stat(r"L1d Misses:\s+(\d+)", out),
        "l1d_rate": extract_stat(r"L1d Hit Rate:\s+([\d\.]+)", out, True),
        "l2_hits": extract_stat(r"L2 Hits:\s+(\d+)", out),
        "l2_miss": extract_stat(r"L2 Misses:\s+(\d+)", out),
        "l2_rate": extract_stat(r"L2 Hit Rate:\s+([\d\.]+)", out, True),
        "l3_hits": extract_stat(r"L3 Hits:\s+(\d+)", out),
        "l3_miss": extract_stat(r"L3 Misses:\s+(\d+)", out),
        "l3_rate": extract_stat(r"L3 Hit Rate:\s+([\d\.]+)", out, True),
        "wb_hits": extract_stat(r"Write Buffer Fwds:\s+(\d+)", out),
        "vc_hits": extract_stat(r"Victim Cache Hits:\s+(\d+)", out),
        "mem_reads": extract_stat(r"Memory Reads.*:\s+(\d+)", out),
        "mem_writes": extract_stat(r"Memory Writes.*:\s+(\d+)", out),
        "total_accesses": extract_stat(r"Total Accesses:\s+(\d+)", out)
    }

# --- MAIN EXECUTION ---
if st.button("Run Simulation", type="primary"):
    if uploaded_file is None:
        st.error("Please upload an executable test file first.")
    else:
        # 1. TRACE GENERATION (Only runs if file or limits changed)
        file_changed = (st.session_state.last_filename != uploaded_file.name)
        limits_changed = (st.session_state.last_max_accesses != max_accesses)
        
        if file_changed or limits_changed or not st.session_state.trace_ready:
            with st.spinner("Running Intel PIN to generate memory trace... (This only happens once per file)"):
                with tempfile.NamedTemporaryFile(delete=False) as tmp:
                    tmp.write(uploaded_file.read())
                    exe_path = tmp.name
                os.chmod(exe_path, 0o755) 

                pin_cmd = [
                    PIN_EXECUTABLE, 
                    "-t", PIN_TOOL, 
                    "-max_accesses", str(max_accesses), 
                    "--", exe_path
                ]
                
                try:
                    subprocess.run(pin_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    st.session_state.trace_ready = True
                    st.session_state.last_filename = uploaded_file.name
                    st.session_state.last_max_accesses = max_accesses
                    st.success("Memory trace generated and cached successfully!")
                except subprocess.CalledProcessError as e:
                    st.error(f"PIN Tool execution failed. Ensure PIN paths are correct.\n{e.stderr.decode()}")
                    st.stop()
                finally:
                    os.unlink(exe_path) # Clean up executable, keep the trace
                    
        # Parse workload characteristics
        with st.spinner("Analyzing Workload Characteristics..."):
            op_counts = {'I': 0, 'R': 0, 'W': 0}
            if os.path.exists("memory_trace.out"):
                with open("memory_trace.out", "r") as f:
                    for line in f:
                        parts = line.strip().split()
                        if len(parts) == 2:
                            if parts[0] in op_counts:
                                op_counts[parts[0]] += 1

        # 2. RUN CACHE SIMULATOR (Main Policy)
        with st.spinner(f"Running Cache Simulation ({rep_policy_str})..."):
            try:
                stats = run_cache_simulator(rep_policy)
            except subprocess.CalledProcessError:
                st.error("Cache Simulator failed.")
                st.stop()

        # 3. RUN ORACLE SIMULATOR (If checked and not already running OPT)
        opt_stats = None
        if run_opt_benchmark and rep_policy != 7:
            with st.spinner("Running Bélády's Optimal Benchmark (Pass 1 & 2)..."):
                try:
                    opt_stats = run_cache_simulator(7)
                except subprocess.CalledProcessError:
                    st.error("Optimal Cache Simulator failed.")
                    st.stop()

        # --- RENDER DASHBOARD ---
        st.subheader("Simulation Results")

        # Workload Profile
        st.markdown("### Workload Profile")
        wA, wB, wC = st.columns(3)
        wA.metric("Instructions Fetched", f"{op_counts['I']:,}")
        wB.metric("Data Reads", f"{op_counts['R']:,}")
        wC.metric("Data Writes", f"{op_counts['W']:,}")
        st.divider()

        # Bélády's Benchmark Chart (If applicable)
        if opt_stats:
            st.markdown(f"### Performance vs. Mathematical Ceiling (Bélády's OPT)")
            
            categories = ['L1i Hit Rate', 'L1d Hit Rate', 'L2 Hit Rate', 'L3 Hit Rate']
            selected_rates = [stats['l1i_rate'], stats['l1d_rate'], stats['l2_rate'], stats['l3_rate']]
            opt_rates = [opt_stats['l1i_rate'], opt_stats['l1d_rate'], opt_stats['l2_rate'], opt_stats['l3_rate']]

            fig_bar = go.Figure(data=[
                go.Bar(name=f'Selected ({rep_policy_str})', x=categories, y=selected_rates, marker_color='#3498db'),
                go.Bar(name='Bélády (Optimal)', x=categories, y=opt_rates, marker_color='#2ecc71')
            ])
            fig_bar.update_layout(barmode='group', yaxis_title='Hit Rate (%)', 
                                  paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="rgba(0,0,0,0)",
                                  font=dict(color="white"))
            fig_bar.update_yaxes(range=[0, 100])
            st.plotly_chart(fig_bar, use_container_width=True)
            st.divider()

        # Cache Hit/Miss Metrics
        c1, c2, c3, c4 = st.columns(4)
        with c1:
            st.markdown("### L1i Cache")
            st.metric("L1i Hit Rate", f"{stats['l1i_rate']}%")
            st.write(f"**Hits:** {stats['l1i_hits']:,}")
            st.write(f"**Misses:** {stats['l1i_miss']:,}")
        with c2:
            st.markdown("### L1d Cache")
            st.metric("L1d Hit Rate", f"{stats['l1d_rate']}%")
            st.write(f"**Hits:** {stats['l1d_hits']:,}")
            st.write(f"**Misses:** {stats['l1d_miss']:,}")
        with c3:
            st.markdown("### L2 Cache")
            st.metric("L2 Hit Rate", f"{stats['l2_rate']}%")
            st.write(f"**Hits:** {stats['l2_hits']:,}")
            st.write(f"**Misses:** {stats['l2_miss']:,}")
        with c4:
            st.markdown("### L3 Cache")
            st.metric("L3 Hit Rate", f"{stats['l3_rate']}%")
            st.write(f"**Hits:** {stats['l3_hits']:,}")
            st.write(f"**Misses:** {stats['l3_miss']:,}")
        st.divider()

        # AMAT & Bus Traffic
        st.markdown("### Performance Estimation")
        
        # 1. Combine L1 Instruction (L1i) and Data (L1d) statistics
        total_l1_accesses = stats['l1i_hits'] + stats['l1i_miss'] + stats['l1d_hits'] + stats['l1d_miss']
        total_l1_misses = stats['l1i_miss'] + stats['l1d_miss']

        # 2. Start AMAT calculation with the base L1 latency
        amat = l1_lat
        
        # 3. Add L2 penalty based on the COMBINED L1 miss rate
        if total_l1_accesses > 0:
            amat += ((total_l1_misses / total_l1_accesses) * l2_lat)
            
        # 4. Add L3 penalty based on L2 miss rate
        if (stats['l2_hits'] + stats['l2_miss']) > 0:
            amat += ((stats['l2_miss'] / (stats['l2_hits'] + stats['l2_miss'])) * l3_lat)
            
        # 5. Add Main Memory penalty based on L3 miss rate
        if (stats['l3_hits'] + stats['l3_miss']) > 0:
            amat += ((stats['l3_miss'] / (stats['l3_hits'] + stats['l3_miss'])) * mem_lat)
            
        col1, col2, col3 = st.columns(3)
        # 6. Update the UI help text to reflect the new global formula
        col1.metric("Average Memory Access Time (AMAT)", f"{amat:.2f} Cycles", help="Calculated using combined Instruction (L1i) and Data (L1d) requests as the baseline.")
        col2.metric("Bus Traffic (Reads)", f"{stats['mem_reads']:,}")
        col3.metric("Bus Traffic (Writes)", f"{stats['mem_writes']:,}")
        st.divider()
        
        # Data Resolution Pie Chart
        st.markdown("### Data Resolution Hierarchy")
        st.markdown("Where did the CPU successfully find the requested data?")
        
        pie_data = {
            "Source": ["L1i Cache", "L1d Cache", "Write Buffer", "Victim Cache", "L2 Cache", "L3 Cache", "Main Memory"],
            "Hits": [stats['l1i_hits'], stats['l1d_hits'], stats['wb_hits'], stats['vc_hits'], stats['l2_hits'], stats['l3_hits'], stats['l3_miss']]
        }
        df = pd.DataFrame(pie_data)
        df = df[df["Hits"] > 0] 

        if not df.empty:
            fig_pie = px.pie(df, values='Hits', names='Source', hole=0.4, color_discrete_sequence=px.colors.sequential.Tealgrn)
            fig_pie.update_layout(paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="rgba(0,0,0,0)", font=dict(color="white"))
            st.plotly_chart(fig_pie, use_container_width=True)
            
        st.divider()
        
        # Memory Access Heatmap
        st.subheader("Memory Access Heatmap")
        st.markdown("This chart visualizes spatial and temporal locality. The **X-axis** is time (sequence of access), and the **Y-axis** is the memory address.")
        
        with st.spinner("Generating heatmap..."):
            try:
                times = []
                addresses = []
                
                with open("memory_trace.out", "r") as f:
                    for i, line in enumerate(f):
                        parts = line.strip().split()
                        if len(parts) == 2:
                            op, addr_hex = parts
                            addr = int(addr_hex, 16)
                            times.append(i)
                            addresses.append(addr)
                            
                if addresses:
                    fig, ax = plt.subplots(figsize=(12, 6))
                    hb = ax.hexbin(times, addresses, gridsize=60, cmap='inferno', bins='log')
                    cb = fig.colorbar(hb, ax=ax)
                    cb.set_label('Log10(Access Count)', color='white')
                    ax.set_title("Memory Access Pattern Over Time", color='white')
                    ax.set_xlabel("Access Sequence (Time)", color='white')
                    ax.set_ylabel("Memory Address", color='white')
                    
                    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, pos: f"0x{int(x):X}"))
                    
                    fig.patch.set_facecolor('#0E1117') 
                    ax.set_facecolor('#0E1117')
                    ax.tick_params(colors='white', which='both')
                    for spine in ax.spines.values():
                        spine.set_color('white')
                    cb.ax.yaxis.set_tick_params(color='white')
                    cb.ax.yaxis.set_tick_params(labelcolor='white')

                    st.pyplot(fig)
                else:
                    st.warning("No valid memory addresses found in the trace file to generate a heatmap.")

            except Exception as e:
                st.error(f"Failed to generate heatmap: {e}")
                
        with st.expander("View Raw Terminal Output"):
            st.code(stats['output'], language="text")