#pragma once
// Display attribute for the composition text (dotted underline), exposed via
// ITfDisplayAttributeProvider. Adapted from Weasel WeaselTSF/DisplayAttribute*.

#include <msctf.h>

namespace t9ime::tip {

// New reference to the enumerator / info object (both hold a DLL reference).
IEnumTfDisplayAttributeInfo* CreateDisplayAttributeEnum();
ITfDisplayAttributeInfo* CreateInputDisplayAttributeInfo();

}  // namespace t9ime::tip
