// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
// Harmless isolated hostile-loader witness, never installed or used by product.
__attribute__((constructor)) static void injected()
{
    setenv("QINDAQT_FIXTURE_INJECTED", "1", 1);
}
