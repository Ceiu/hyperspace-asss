
/* dist: public */

#ifndef __BWLIMIT_H
#define __BWLIMIT_H

/* these are the available classes/priorities of traffic */
enum
{
	BW_UNREL_LOW,
	BW_UNREL,
	BW_UNREL_HIGH,
	BW_REL,
	BW_ACK,
	BW_PRIS
};

typedef struct BWLimit BWLimit;

#define I_BWLIMIT "bwlimit-3"

typedef struct Ibwlimit
{
	INTERFACE_HEAD_DECL

	BWLimit * (*New)();
	void (*Free)(BWLimit *bw);

	/* adjust the current idea of how many bytes have been sent
	 * recently. call once in a while. now is in millis, not ticks. */
	void (*Iter)(BWLimit *bw, ticks_t now);

	/* checks if <bytes> bytes at priority <pri> can be sent according to
	 * the current limit and sent counters. if they can be sent, modifies bw
	 * and returns true, otherwise returns false. */
	int (*Check)(BWLimit *bw, int bytes, int pri);

	void (*AdjustForAck)(BWLimit *bw);
	void (*AdjustForRetry)(BWLimit *bw);

	int (*GetCanBufferPackets)(BWLimit *bw);

	void (*GetInfo)(BWLimit *bw, char *buf, int buflen);
} Ibwlimit;

#endif

