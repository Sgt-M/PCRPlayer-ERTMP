// PcrFilter.h : bundled DirectShow filter per-user registration
#pragma once

namespace pcrfilters
{
	// Register the filters shipped under <exedir>\filters\{x64|x86}\ into
	// HKCU\Software\Classes\CLSID so the classic DShow device enumerator
	// (and therefore the existing decoder-selection UI) can see them.
	// No admin rights needed; never touches filters already registered
	// machine-wide. Idempotent + version-stamped.
	void EnsureRegistered();
}
