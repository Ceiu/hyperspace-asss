
# dist: public

import asss
import sys

lm = asss.get_interface(asss.I_LOGMAN)

# log uncaught exceptions using logman
def excepthook(type, value, tb):
    import traceback
    tblines = traceback.format_exception(type, value, tb)

    for line in tblines:
        lm.Log(asss.L_ERROR, "<pymod> "+line)
    sys.__excepthook__(type, value, traceback)

sys.excepthook = excepthook
