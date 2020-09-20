#ifndef __GRR_NFA_DEF_H__
#define __GRR_NFA_DEF_H__

enum grrRetValue {
    GRR_RET_OK = 0,
    GRR_RET_NOT_IMPLEMENTED,
    GRR_RET_BREAK_LOOP,
    GRR_RET_NOT_FOUND,
    GRR_RET_OUT_OF_MEMORY,
    GRR_RET_BAD_DATA,
    GRR_RET_FILE_ACCESS,
    GRR_RET_OVERFLOW,
    GRR_RET_MISSING_ENVIRONMENT_VARIABLE,
};

typedef struct grrNfaStruct *grrNfa;

#endif // __GRR_NFA_DEF_H__
