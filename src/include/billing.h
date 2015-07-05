
/* dist: public */

#ifndef __BILLING_H
#define __BILLING_H

typedef enum
{
	/* pyconst: enum, "BILLING_*" */
	BILLING_DISABLED = 0,
	BILLING_DOWN = 1,
	BILLING_UP = 2
} billing_state_t;

#define I_BILLING "billing-1"

typedef struct Ibilling
{
	INTERFACE_HEAD_DECL

	billing_state_t (*GetStatus)(void);
	/* returns one of the above constants */

	int (*GetIdentity)(byte *buf, int len);
	/* returns number of bytes written to buf, -1 on failure */

} Ibilling;

#endif

