#ifndef HS_UTIL_ENERGY_H
#define HS_UTIL_ENERGY_H

#define I_HS_UTIL_ENERGY "hs_util_energy-1"

enum
{
	UTILITY_ENERGY_SPAWNED = 1,
	UTILITY_ENERGY_KILLED,
	UTILITY_ENERGY_MODIFIED,
	UTILITY_ENERGY_SET,
	UTILITY_ENERGY_RECHARGED,
	UTILITY_ENERGY_DRAINED,
	UTILITY_ENERGY_STATE_SET,
};

//Utility "state" (on or off) just changed.
#define CB_UTILITY_STATE_CHANGED "utilitystatechanged-1"
typedef void (*UtilityStateChanged)(Player *p, int state, u8 reason);

typedef struct Ihsutilenergy
{
	INTERFACE_HEAD_DECL

    void (*Lock)(Arena *arena);
    void (*Unlock)(Arena *arena);
    
    //Returns 1 if player has a utility item, 0 otherwise.
    //Returns -1 if the module is not attached to the player's arena.
	int (*HasUtility)(Player *p); 
	
	//Returns a player's current utility energy. 
	double (*GetUtilityEnergy)(Player *p); 
	
	void (*ModUtilityEnergy)(Player *p, double amount); //Increases utility energy by specific amount
	void (*SetUtilityEnergy)(Player *p, double energy); //Sets utility energy to specified amount
	
	//Set state to 1 to turn this player's utility item on, set state to 0 to turn it off
	void (*SetUtilityState)(Player *p, int state);
} Ihsutilenergy;

#endif //HS_UTIL_ENERGY_H
