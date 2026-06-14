1. If any specification is not clear, contradicts itself or is ambiguous ask user for clarification.
2. Restrict all code to very simple control flow constructs—do not use goto statements, setjmp or longjmp constructs, or direct or indirect recursion.
3. Give all loops a fixed upper bound.
4. Do not use dynamic memory allocation after initialization.
5. No function should be longer than about 60 lines of code.
6. The code's assertions density should average to minimally two assertions per function. Assertions must be used to check for anomalous conditions that should never happen in real-life executions.
7. Each calling function must check the return value of nonvoid functions, and each called function must check the validity of all parameters provided by the caller.
8. The use of the preprocessor must be limited to the inclusion of header files and simple macros definitions. Token pasting, variable argument lists (ellipses), and recursive macro calls are not allowed. All macros must expand into complete syntactic units. The use of conditional compilation directives must be kept to a minimum.
9. The use of pointers must be restricted. Specifically, no more than one level of dereference should be used. Pointer dereference operations may not be hidden in macro definitions or inside typedef declarations.
10. All code must be compiled, from the first day of development, with all compiler warnings enabled. All code must compile without warnings.
11. Use `sys_xxx` for system peripheral-related files (e.g. BLE or RF) and `fw_xxx` for firmware-only related files (e.g. session handling)
12. Use `bleid` for generating BluetoothLE UUIDs (help command is available)
13. Ensure BLE devices are re-advertised after disconnected.
14. Author: H1M4W4R1
15. Folders: `drivers/` - for peripheral-related code, `systems/` for low-level systems `operation/` for user-level handling (HMI expect UI), `ui/` for user interface
16. Version tracking starts at `1.0.1`. For every user-facing firmware, BLE, or documentation change, automatically update `README.MD` current version and changelog using semantic versioning unless the user specifies an exact version. Keep changelog entries newest first.
