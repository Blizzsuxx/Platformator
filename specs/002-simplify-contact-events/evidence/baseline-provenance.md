# Baseline provenance

Revision: `f67f6ba47f88f08ee2dd2aae1ea8eee940aa07d8`. Existing staged feature documents preserved. Initial index/worktree patches retained beside this file.

Linux/GCC, existing cleanup-debug/work/benchmark/asan/tsan presets. Release timings use workers 1 (CPU 0) and 6 (CPUs 0,2,4,6,8,10), seed 0, dt 1/120, warmup 120, measured 600, five independent processes.

Confirmed source boundaries: geometry workers mutate their pair only; serial contact/support reduction follows their join; Grid calls removePair before candidate erasure; pair destructor currently emits Exit. Script startup and SCRIPT reconciliation currently refresh observer memberships; body reconciliation also needs physical solver refresh. contactPairs remains necessary for benchmark_main validatePhysicalState. Existing ignore rules cover C++/Python build artifacts; no Docker/JS/Terraform/Helm setup found.

```text
Architecture:                            x86_64
CPU op-mode(s):                          32-bit, 64-bit
Address sizes:                           48 bits physical, 48 bits virtual
Byte Order:                              Little Endian
CPU(s):                                  12
On-line CPU(s) list:                     0-11
Vendor ID:                               AuthenticAMD
Model name:                              AMD Ryzen 5 7535HS with Radeon Graphics
CPU family:                              25
Model:                                   68
Thread(s) per core:                      2
Core(s) per socket:                      6
Socket(s):                               1
Stepping:                                1
Microcode version:                       0xa404108
Frequency boost:                         enabled
CPU(s) scaling MHz:                      84%
CPU max MHz:                             4604.7568
CPU min MHz:                             416.0930
BogoMIPS:                                6587.45
Flags:                                   fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx mmxext fxsr_opt pdpe1gb rdtscp lm constant_tsc rep_good nopl xtopology nonstop_tsc cpuid extd_apicid aperfmperf rapl pni pclmulqdq monitor ssse3 fma cx16 sse4_1 sse4_2 x2apic movbe popcnt aes xsave avx f16c rdrand lahf_lm cmp_legacy svm extapic cr8_legacy abm sse4a misalignsse 3dnowprefetch osvw ibs skinit wdt tce topoext perfctr_core perfctr_nb bpext perfctr_llc mwaitx cpb cat_l3 cdp_l3 hw_pstate ssbd mba ibrs ibpb stibp vmmcall fsgsbase bmi1 avx2 smep bmi2 erms invpcid cqm rdt_a rdseed adx smap clflushopt clwb sha_ni xsaveopt xsavec xgetbv1 xsaves cqm_llc cqm_occup_llc cqm_mbm_total cqm_mbm_local user_shstk clzero irperf xsaveerptr rdpru wbnoinvd cppc arat npt lbrv svm_lock nrip_save tsc_scale vmcb_clean flushbyasid decodeassists pausefilter pfthreshold avic v_vmsave_vmload vgif v_spec_ctrl umip pku ospke vaes vpclmulqdq rdpid overflow_recov succor smca fsrm debug_swap
Virtualization:                          AMD-V
L1d cache:                               192 KiB (6 instances)
L1i cache:                               192 KiB (6 instances)
L2 cache:                                3 MiB (6 instances)
L3 cache:                                16 MiB (1 instance)
NUMA node(s):                            1
NUMA node0 CPU(s):                       0-11
Vulnerability Gather data sampling:      Not affected
Vulnerability Ghostwrite:                Not affected
Vulnerability Indirect target selection: Not affected
Vulnerability Itlb multihit:             Not affected
Vulnerability L1tf:                      Not affected
Vulnerability Mds:                       Not affected
Vulnerability Meltdown:                  Not affected
Vulnerability Mmio stale data:           Not affected
Vulnerability Old microcode:             Not affected
Vulnerability Reg file data sampling:    Not affected
Vulnerability Retbleed:                  Not affected
Vulnerability Spec rstack overflow:      Mitigation; Safe RET
Vulnerability Spec store bypass:         Mitigation; Speculative Store Bypass disabled via prctl
Vulnerability Spectre v1:                Mitigation; usercopy/swapgs barriers and __user pointer sanitization
Vulnerability Spectre v2:                Mitigation; Retpolines; IBPB conditional; IBRS_FW; STIBP always-on; RSB filling; PBRSB-eIBRS Not affected; BHI Not affected
Vulnerability Srbds:                     Not affected
Vulnerability Tsa:                       Mitigation; Clear CPU buffers
Vulnerability Tsx async abort:           Not affected
Vulnerability Vmscape:                   Mitigation; IBPB before exit to userspace
```

Common fixture SHA-256: `feccbca0bb87526738d8fc1aae1e218fbe80caf3998cd7c373bedc46dfc84b51`. Runner smoke verified recipients 0/1/2, ten real pairs, and rejection of recipients -1/3 and zero pair count. Python fixture tests: 8 passed.

Supplemental fixture boundary correction before candidate captures: zero-warmup runs initially checked the expected contact population before any geometry evaluation. The initial-state check now explicitly distinguishes contacts-not-yet-evaluated, with all post-step/final checks strict; a runner CTest regression covers this case. The scene builder, geometry, captured warmup=120 workloads and measured complete-step boundaries remain unchanged from the common baseline fixture.
