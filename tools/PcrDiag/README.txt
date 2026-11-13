PcrDiag - bundled filter registration diagnostics.
Build x64 with vcvars64 and x86 with vcvars32:
  cl /EHsc /DUNICODE /D_UNICODE /O2 PcrDiag.cpp /Fe:PcrDiag64.exe /link shlwapi.lib strmiids.lib ole32.lib advapi32.lib user32.lib
  (x86 build outputs PcrDiag.exe)
Ship PcrDiag64.exe + PcrDiag.exe next to PCRPlayer exes; running either writes PcrDiag.txt
in the same folder ('batch' argument suppresses the message box).
