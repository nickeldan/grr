#ifndef __GRR_NFA_DEF_H__
#define __GRR_NFA_DEF_H__

/*! \enum grrRetValue
    \brief Function return values.

    Any function in the Grr API which returns an int returns one of these
    value.
*/
enum grrRetValue {
    GRR_RET_OK = 0, //!< The function succeeded.
    GRR_RET_NOT_IMPLEMENTED, //!< The requested functionality has not yet been implemented.
    GRR_RET_DONE, //!< A recursive task has finished.
    GRR_RET_NOT_FOUND, //!< The requested item was not found.
    GRR_RET_OUT_OF_MEMORY, //!< Memory allocation failure.
    GRR_RET_BAD_DATA, //!< Invalid data was passed to the function.
    GRR_RET_FILE_ACCESS, //!< A file read/write failed.
    GRR_RET_OVERFLOW, //!< A buffer overflow occurred.
    GRR_RET_EXEC, //!< A call to exec failed.
    GRR_RET_OTHER, //!< A generic error occurred which is not covered by the above options.
};

/*! \typedef grrNfa
    \brief An opaque reference to Grr's regex object.
*/
typedef struct grrNfaStruct *grrNfa;

#endif // __GRR_NFA_DEF_H__
