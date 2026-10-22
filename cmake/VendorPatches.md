# Local dependency patches

- SDL3_mixer 3.2.0, `src/decoder_wav.c`: signed 24-bit WAV conversion multiplies the signed high byte by 65536 instead of left-shifting a negative integer. UBSan reproduced the undefined shift while decoding the existing jump WAV fixture. The decoded value and supported sample range are unchanged.
- The TSan preset keeps oneTBB compiler warnings visible but disables its warnings-as-errors option. GCC 16 warns that `atomic_thread_fence` cannot be modeled by TSan; engine and TBB remain instrumented, and runtime race reports are not suppressed.
