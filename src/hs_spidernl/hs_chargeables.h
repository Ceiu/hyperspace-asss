#ifndef HS_CHARGEABLES_H
#define HS_CHARGEABLES_H

#define I_HS_CHARGEABLES "hs_chargeables-1"

enum
{
	CHARGEABLES_REASON_SPAWNED = 1,
	CHARGEABLES_REASON_KILLED,
	CHARGEABLES_REASON_DEFAULT
};

//Charging "state" (on or off) just changed.
#define CB_CHARGEABLE_STATE_CHANGED "chargeablestatechanged-1"
typedef void (*ChargeableStateChanged)(Player *p, int state, u8 reason);

typedef struct Ihschargeables
{
	INTERFACE_HEAD_DECL

    void (*Lock)(Arena *arena);
    void (*Unlock)(Arena *arena);
    
    //Returns 1 if player has a Chargeable item, 0 otherwise.
    //Returns -1 if the module is not attached to the player's arena.
	int (*HasChargeable)(Player *p); 
	
	//Returns a player's current 'charge'. 
	double (*GetCharge)(Player *p); 
	
	void (*ModCharge)(Player *p, double amount); //Increases player's 'charge' by specified amount
	void (*SetCharge)(Player *p, double charge); //Sets player's 'charge' to specified amount
	
	//Set state to 1 to set Chargeables item to 'charged' state, 0 to set it to 'uncharged' state.
	void (*SetIsCharged)(Player *p, int charged);
} Ihschargeables;

#endif
