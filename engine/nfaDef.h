/**
 * \file    nfaDef.h
 * \brief   Basic Grr definitions.
 */

#ifndef __GRR_NFA_DEF_H__
#define __GRR_NFA_DEF_H__

/**
 * \brief Function return values.
 */
enum grrRetValue {
    /// The function succeeded.
    GRR_RET_OK = 0,
    /// The requested functionality has not yet been implemented.
    GRR_RET_NOT_IMPLEMENTED,
    /// A recursive task has finished.
    GRR_RET_DONE,
    /// The requested item was not found.
    GRR_RET_NOT_FOUND,
    /// Memory allocation failure.
    GRR_RET_OUT_OF_MEMORY,
    /// Invalid data was passed to the function.
    GRR_RET_BAD_DATA,
    /// A file read/write failed.
    GRR_RET_FILE_ACCESS,
    /// A buffer overflow occurred.
    GRR_RET_OVERFLOW,
    /// A call to exec failed.
    GRR_RET_EXEC,
    /// A generic error occurred which is not covered by the above options.
    GRR_RET_OTHER,
};

/**
 * \brief   An opaque reference to Grr's regex object.
 */
typedef struct grrNfaStruct *grrNfa;

#endif // __GRR_NFA_DEF_H__
