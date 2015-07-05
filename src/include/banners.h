
/* dist: public */

#ifndef __BANNERS_H
#define __BANNERS_H

#include "packets/banners.h"

#define CB_SET_BANNER "setbanner"
typedef void (*SetBannerFunc)(Player *p, Banner *banner, int from_player);
/* called when the player sets a new banner */
/* pycb: player, banner, int */


#define I_BANNERS "banners-3"

typedef struct Ibanners
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	void (*SetBanner)(Player *p, Banner *banner);
	/* sets banner. NULL to remove. */
	/* pyint: player, banner -> void */
	void (*SetBannerFromPlayer)(Player *p, Banner *banner);
	/* sets banner. NULL to remove. Changes will be sent 
	 * on to the billing server. */
	/* pyint: player, banner -> void */
} Ibanners;

#endif

