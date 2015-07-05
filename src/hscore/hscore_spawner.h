#ifndef HSCORE_SPAWNER_H
#define HSCORE_SPAWNER_H

#define I_HSCORE_SPAWNER "hscore_spawner-5"
typedef struct Ihscorespawner
{
	INTERFACE_HEAD_DECL

	//void (*respawn)(Player *p); //called when the player has just been shipreset
	void (*resendOverrides)(Player *p); //recalculates client settings and sends them

	int (*getFullEnergy)(Player *p);
	void (*ignorePrize)(Player *p, int prize);
} Ihscorespawner;

#define A_HSCORE_SPAWNER "hscore_spawner-6"
typedef struct Ahscorespawner
{
	ADVISER_HEAD_DECL

	/**
	Called during addOverrides when the specified property is being
	'handled'. Return whichever value you want the clientside property
	to be set to.
	*/
	int (*getOverrideValue)(Player *p, int ship, int shipset, const char *prop, int init_value);
} Ahscorespawner;

#endif //HSCORE_SPAWNER_H
