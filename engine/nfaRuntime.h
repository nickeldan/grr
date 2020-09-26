#ifndef __GRR_RUNTIME_H__
#define __GRR_RUNTIME_H__

#include <stdbool.h>
#include <sys/types.h>

#include "nfaDef.h"

/*! \fn grrMatch
    \brief Determines if the entire string matches the regex.
    \param nfa The Grr regex object.
    \param string The string (does not have to be null-terminated).
    \param len The length of the string.
    \return GRR_RET_OK if the string matched the regex and an error code otherwise.

    Possible error codes:
        - GRR_RET_NOT_FOUND (the string did not match the regex)
        - GRR_RET_OUT_OF_MEMORY
        - GRR_RET_BAD_DATA (the string contained non-printable characters)
*/
int grrMatch(const grrNfa nfa, const char *string, size_t len);

/*! \fn grrSearch
    \brief Determines if a string contains a substring which matches the regex.
    \param nfa The Grr regex object.
    \param string The string (does not have to be null-terminated).
    \param len The length of the string.
    \param start A pointer which will, if not NULL, point to the index of the beginning of the longest match if one was found.
    \param end A pointer which will, if not nULL, point to the index of the character after the end of the longest match if one was found.
    \param cursor A pointer which will, if not NULL, point to the index of the character where the function stopped searching.
    \param tolerateNonprintables Indicates whether or not the function should exit upon encountering a non-printable character.
    \return GRR_RET_OK if a substring match was found and an error code otherwise.

    The function will stop processing characters when either it exhausts the specified length, a newline is encountered, or a non-printable character is encountered with tolerateNonprintables set to false.

    If tolerateNonprintables is true, then the function will skip over such characters and treat them as beginning and end of lines.  For example, if the regex is "^a+" and the string is "aa\x00aaad", then both sequence of "a"'s will be considered matches.  However, they will be considered separate matches and so *end-*start will be 3.

    Possible error codes:
        - GRR_RET_NOT_FOUND (no substring matched the regex)
        - GRR_RET_BAD_DATA (a non-printable character, other than a new line, was encountered and toleratePrintables was set to false)
*/
int grrSearch(const grrNfa nfa, const char *string, size_t len, size_t *start, size_t *end, size_t *cursor, bool tolerateNonprintables);

#endif // __GRR_RUNTIME_H__
