/// @file nfa.h
/**
 * \file    nfa.h
 * \brief   Grr interface portal.
 */

#ifndef __GRR_NFA_H__
#define __GRR_NFA_H__

#include "nfaDef.h"
#include "nfaCompiler.h"
#include "nfaRuntime.h"

/**
 * \brief           Frees a regex object.
 *
 * \note            Returns immediately if nfa is NULL
 *
 * \param nfa       A Grr regex object.
 */
void grrFreeNfa(grrNfa nfa);

/**
 * \brief       Returns the string that created the regex object.
 *
 * \note        The const modifier of the returned string is to forbid the user
 *              from either altering or freeing the string since it is inherent
 *              to the regex object.
 *
 * \param nfa   A Grr regex object.
 * \return      The string which created the regex object.
 */
const char *grrDescription(const grrNfa nfa);

#endif // __GRR_NFA_H__
