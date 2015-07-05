#ifndef FLAGUTIL_H
#define FLAGUTIL_H

#define I_FLAGUTIL "FlagUtil-1"

typedef struct
{
    INTERFACE_HEAD_DECL
    
    void (*ResetFlagTimer)(Player *p);
    
} Iflagutil;

#endif
