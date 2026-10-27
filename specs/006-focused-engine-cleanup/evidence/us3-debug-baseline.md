# Debug baseline

The baseline type is not nothrow-move-constructible under the debug configuration (checked using the baseline's recorded compiler flags). `DebugObject` owns vertex/name data and the destructor suppresses implicit move generation. Actual copy/move/allocation counts and graphical captures have not been obtained; they remain unverified. The release benchmark preset disables debug tools, so its debug mode cannot establish overlay output.

A separate 1,000-object debug growth fixture observed 11 growths and 1,023 replaced vertex buffers in baseline. The matching candidate preserved all 1,023; see `work-memory.md` for method and limits. No rendered overlay capture has been obtained.
