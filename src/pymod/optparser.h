#ifndef OPTPARSER_H
#define OPTPARSER_H

/*
 * OptParser Interface for C Modules
 * D1st0rt, SSCE Hyperspace
 * License: MIT/X11
 * Created: 2009-02-17
 * Updated: 2009-03-14
 *
 * Wrapper for the OptionParser class, because it is way easier than
 * string manipulation in C.
 */

/* pyinclude: pymod/optparser.h */

#define I_OPTPARSER "optparser"

/** This module wraps Python's OptionParser class for use in command
 * (or other) string parsing by C modules.
 */
typedef struct Ioptparser
{
    INTERFACE_HEAD_DECL
    /* pyint: impl */

    /** Wraps OptionParser.add_option.
     * @param group the unique identifier of the parser to use
     * @param name the variable this option will be stored in
     * @param shortopt the short flag ("s" will become "-s")
     * @param longopt the long flag ("long" will become "--long=")
     * @param the type of data expected for this option
     * @return 1 if success, 0 if failure
     * @see http://docs.python.org/library/optparse
     */
    int (*AddOption)(const char *group, const char *name, const char *shortopt, const char *longopt, const char *type);
    /* pyint: string, string, string, string, string -> int */

    /** Clears the parser and any stored values for a given identifier.
     * @param group the unique identifier of the parser to use
     * @return 1 if success, 0 if failure
     */
    int (*FreeGroup)(const char *group);
    /* pyint: string -> int */

    /** Wraps OptionParser.parse_args.
     * @param group the unique identifier of the parser to use
     * @param params the string to parse
     * @return 1 if success, 0 if failure
     * @see http://docs.python.org/library/optparse
     */
    int (*Parse)(const char *group, const char *params);
    /* pyint: string, string -> int */

    /** Gets the integer value of an option.
     * @param group the unique identifier of the parser to use
     * @param name the name of the option ("name" from AddOption)
     * @param val pointer to an int where the option value will be set
     * @return 1 if success, 0 if failure
     */
    int (*GetInt)(const char *group, const char *name, int *val);
    /* pyint: string, string, int out -> int */

    /** Gets the floating point value of an option.
     * @param group the unique identifier of the parser to use
     * @param name the name of the option ("name" from AddOption)
     * @param val pointer to a double where the option value will be set
     * @return 1 if success, 0 if failure
     */
    int (*GetFloat)(const char *group, const char *name, double *val);
    /* pyint: string, string, double out -> int */

    /** Gets the string value of an option.
     * @param group the unique identifier of the parser to use
     * @param name the name of the option ("name" from AddOption)
     * @param val pointer to a string where the option value will be set
     * @return 1 if success, 0 if failure
     */
    int (*GetString)(const char *group, const char *name, char *val, int buflen);
    /* pyint: string, string, string out, int buflen -> int */

    /** Gets the boolean value of an option.
     * @param group the unique identifier of the parser to use
     * @param name the name of the option ("name" from AddOption)
     * @param val pointer to an int where the option value will be set
     * @return 1 if success, 0 if failure
     */
    int (*GetBool)(const char *group, const char *name, int *val);
    /* pyint: string, string, int out -> int */

} Ioptparser;

#endif
