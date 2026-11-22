// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the PS5's libSceCommonDialog, for which the Payload SDK has no import library, as the
// libScePad stub in tools/gen-scepad-stub.py is one: a list of the names that are imports. The one
// function here is the dialogs' start, which the console's keyboard needs before it opens
// (port/app/ime_dialog.cpp). tools/link.sh builds it with -soname libSceCommonDialog.sprx and
// tools/package.sh gives it to the converter, which turns the symbol into an import of that module;
// nothing here is linked into the app or ever runs. The console's loader binds the slot when
// sysmodule 0x0096, the keyboard's, is loaded.

int sceCommonDialogInitialize(void) { return -1; }
