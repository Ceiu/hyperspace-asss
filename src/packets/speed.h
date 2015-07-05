
/* dist: public */

#ifndef __PACKETS_SPEED_H
#define __PACKETS_SPEED_H

#pragma pack(push,1)

struct SpeedStats
{
	u8 type, best;
	u16 rank;
	u32 pscore, score1, score2, score3, score4, score5;
	u16 pid1, pid2, pid3, pid4, pid5;
};

/* If this is the user's best game recorded, best is 1, else 0
 * rank is their personal rank this game, with pscore being their score
 * if not enough players, pid's will be -1 with score being 0
 */

#pragma pack(pop)

#endif

