#ifndef __GRR_NFA_H__
#define __GRR_NFA_H__

#include "nfaDef.h"
#include "nfaCompiler.h"
#include "nfaRuntime.h"

/*! \fn grrFreeNfa
    \brief Frees a regex object.
    \param nfa A Grr regex object.
    \return Nothing.

    Returns immediately if nfa is NULL.
*/
void grrFreeNfa(grrNfa nfa);

/*! \fn grrDescription
    \brief Returns the string that created the regex object.
    \param nfa A Grr regex object.
    \return The string which created the regex object.

    The const modifier of the returned string is to forbid the user from either
    altering or freeing the string since it is inherent to the regex object.
*/
const char *grrDescription(const grrNfa nfa);

#endif // __GRR_NFA_H__
