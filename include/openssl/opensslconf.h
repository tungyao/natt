#pragma once

// Local compatibility shim for environments that install BoringSSL headers
// without the matching opensslconf.h. The BoringSSL headers only require this
// file to exist; target-specific width/thread macros come from target.h.
