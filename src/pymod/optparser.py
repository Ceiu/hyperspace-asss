##
## OptParse Interface for C Modules
## D1st0rt, SSCE Hyperspace
## License: MIT/X11
## Created: 2009-02-17
## Updated: 2009-02-18
##
## Wrapper for the OptionParser class, because it is way easier than
## string manipulation in C.
##
import asss
from optparse import OptionParser, OptionError

parsers = {}
options = {}

class OptParser:
    iid = asss.I_OPTPARSER

    def AddOption(self, group, name, sopt, lopt, tname):
        if group not in parsers:
            parser = OptionParser(usage="", version="")
            parsers[group] = parser
        else:
            parser = parsers[group]

        try:
            if tname == 'bool':
                parser.add_option("-%s" % sopt, "--%s" % lopt, dest=name, action="store_true")
            else:
                parser.add_option("-%s" % sopt, "--%s" % lopt, dest=name, type=tname)
        except (OptionError, TypeError, AttributeError):
            return (0,)

        return (1,)

    def FreeGroup(self, group):
        if group in parsers:
            del parsers[group]
            if group in options:
                del options[group]

            return (1,)

        return (0,)

    def Parse(self, group, params):
        try:
            parser = parsers[group]
            (opts, args) = parser.parse_args(params.split())

            if group not in options:
                options[group] = {}
            for o in vars(opts):
                options[group][o] = getattr(opts, o)

        except (OptionError, TypeError, AttributeError, KeyError):
            return (0,)

        return (1,)

    def GetInt(self, group, name):
        try:
            val = int(options[group][name])
            return (1,val)
        except (TypeError, KeyError, ValueError):
            return (0,0)

    def GetFloat(self, group, name):
        try:
            val = float(options[group][name])
            return (1,val)
        except (TypeError, KeyError, ValueError):
            return (0,0)

    def GetString(self, group, name):
        try:
            val = str(options[group][name])
            return (1,val)
        except (TypeError, KeyError, ValueError):
            return (0,0)

    def GetBool(self, group, name):
        try:
            val = bool(options[group][name])
            return (1,int(val))
        except (TypeError, KeyError, ValueError):
            return (0,0)

intref = asss.reg_interface(OptParser(), None)
