
/* dist: public */

#ifndef __LAGDATA_H
#define __LAGDATA_H


/* querying lag data */

struct PingSummary
{
	/* pytype: struct, struct PingSummary, pingsummary */
	int cur, avg, min, max;
	/* only used for QueryCPing: */
	int s2cslowtotal;
	int s2cfasttotal;
	short s2cslowcurrent;
	short s2cfastcurrent;
};

struct PLossSummary
{
	/* pytype: struct, struct PLossSummary, plosssummary */
	double s2c, c2s, s2cwpn;
};

struct ReliableLagData
{
	/* pytype: struct, struct ReliableLagData, reliablelagdata */
	/* dups is the total number of duplicates that have been recieved,
	 * c2sn is the reliable seqnum so far (i.e., the number of reliable
	 * packets that should have been recieved, excluding dups). */
	unsigned int reldups, c2sn;
	/* retries is the number of times the server has had to re-send a
	 * reliable packet. s2cn is the number of reliable packets that
	 * should have been sent, excluding retries. */
	unsigned int retries, s2cn;
};

struct TimeSyncHistory
{
#define TIME_SYNC_SAMPLES 10
	unsigned int servertime[TIME_SYNC_SAMPLES];
	unsigned int clienttime[TIME_SYNC_SAMPLES];
	int next, drift;
};


#define I_LAGQUERY "lagquery-3"

typedef struct Ilagquery
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*QueryPPing)(Player *p, struct PingSummary *ping);
	/* pyint: player, pingsummary out -> void */
	void (*QueryCPing)(Player *p, struct PingSummary *ping);
	/* pyint: player, pingsummary out -> void */
	void (*QueryRPing)(Player *p, struct PingSummary *ping);
	/* pyint: player, pingsummary out -> void */

	void (*QueryPLoss)(Player *p, struct PLossSummary *d);
	/* pyint: player, plosssummary out -> void */
	void (*QueryRelLag)(Player *p, struct ReliableLagData *d);
	/* pyint: player, reliablelagdata out -> void */

	void (*QueryTimeSyncHistory)(Player *p, struct TimeSyncHistory *d);

	void (*DoPHistogram)(Player *p,
			void (*callback)(Player *p, int bucket, int count, int maxcount, void *clos),
			void *clos);
	/* pyint: player, (player, int, int, int, clos -> void), clos -> void */

	void (*DoRHistogram)(Player *p,
			void (*callback)(Player *p, int bucket, int count, int maxcount, void *clos),
			void *clos);
	/* pyint: player, (player, int, int, int, clos -> void), clos -> void */
} Ilagquery;



/* collecting data */

struct ClientLatencyData
{
	/* all what the client reports */
	unsigned int weaponcount;
	unsigned int s2cslowtotal;
	unsigned int s2cfasttotal;
	unsigned short s2cslowcurrent;
	unsigned short s2cfastcurrent;
	unsigned short unknown1;
	short lastping;
	short averageping;
	short lowestping;
	short highestping;
};

struct TimeSyncData
{
	/* what the server thinks */
	unsigned int s_pktrcvd, s_pktsent;
	/* what the client reports */
	unsigned int c_pktrcvd, c_pktsent;
	/* time sync */
	unsigned int s_time, c_time;
};


#define I_LAGCOLLECT "lagcollect-4"

typedef struct Ilagcollect
{
	INTERFACE_HEAD_DECL

	void (*Position)(Player *p, int ms, int cliping, unsigned int wpnsent);
	void (*RelDelay)(Player *p, int ms);
	void (*ClientLatency)(Player *p, struct ClientLatencyData *data);
	void (*TimeSync)(Player *p, struct TimeSyncData *data);
	void (*RelStats)(Player *p, struct ReliableLagData *data);

	void (*Clear)(Player *p);

} Ilagcollect;

#endif

