
/* dist: public */

#ifndef __PATHUTIL_H
#define __PATHUTIL_H

/** @file
 * utility functions for working with search paths and things */


/** describes one replaecement for macro_expand_string */
struct replace_table
{
	int repl;          /**< which character to replace */
	const char *with;  /**< the string to replace it with */
};

/** Expands a string containing two-character macro sequences into a
 ** destination buffer, using a provided table of replacements.
 * The source string can contain arbitrary text, as well as
 * two-character sequences, like "%x", which will be expanded according
 * to the replacement table. The escape character ("%" in the above
 * example) is determined by the caller. There should be one
 * replace_table struct for each sequence you want to replace. Double
 * the macro character in the source string to insert it into the output
 * by itself.
 *
 * @param dest where to put the result
 * @param destlen how much space the result can hold
 * @param source the source string
 * @param repls a pointer to an array of struct replace_table
 * @param replslen how many replacements in the table
 * @param macrochar which character to use as the escape code
 * @return the number of characters in the destionation string, or -1 on
 * error
 */
int macro_expand_string(
		char *dest,
		int destlen,
		char *source,
		struct replace_table *repls,
		int replslen,
		char macrochar);

/** Finds the first of a set of pattern-generated filenames that exist.
 * This walks through a search path, expanding each element with
 * macro_expand_string, and checking if the resulting string refers to a
 * file that exists. If so, it puts the result in dest. If none match,
 * or if there was an error expanding a source string, it returns an
 * error.
 *
 * @param dest where to put the result
 * @param destlen how much space the result can hold
 * @param searchpath a colon-delimited list of strings, where each
 * string is a source string acceptable by macro_expand_string
 * @param repls the replacement table to use
 * @param replslen how many entries in the replacement table
 * @return 0 on success, -1 on failure
 */
int find_file_on_path(
		char *dest,
		int destlen,
		const char *searchpath,
		struct replace_table *repls,
		int replslen);

/** Checks if the given path is secure against trying to access files
 ** outside of the server root.
 * Currently this checks for initial or trailing /, nonprintable chars,
 * colons, double dots, double slashes, and double backslashes.
 * Any of the above conditions makes it return failure.
 * @param path the path to check
 * @return true if the given path is "safe", false otherwise
 */
int is_valid_path(const char *path);

/** Gets a pointer to the basename of a path.
 * If the path contains several segments separated by slash or backslash
 * delimiters, get_basename returns a pointer to the last segment.
 * If it's only one segment, it returns the whole string.
 * @param path the path to get the basename of
 * @return a pointer to the basename of the given path
 */
const char *get_basename(const char *path);

#endif

