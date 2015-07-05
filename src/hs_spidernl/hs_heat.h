#ifndef HS_HEAT_H
#define HS_HEAT_H

#define I_HS_HEAT "hs_heat-1"

//This callback is called just before hs_heat resends overrides, so that
//modules that also require resent overrides don't have to send their own
//(if they rely on heat).
#define CB_HEAT_STATE_CHANGED "heatstatechanged-1"
typedef void (*HeatStateChanged)(Player *p, int state);

typedef struct Ihsheat
{
	INTERFACE_HEAD_DECL
	
	//Returns a player's current heat.
    double (*GetHeat)(Player *p);
    
    //Returns 1 if the player is currently overheated, 0 if not.
    int (*IsOverheated)(Player *p);
    
    //Modifies the player's current heat by the specified amount.
    void (*ModHeat)(Player *p, double amount);
    void (*ModHeatNoLock)(Player *p, double amount);
    
    //Sets the player's current heat to the specified amount.
    void (*SetHeat)(Player *p, double heat);
    void (*SetHeatNoLock)(Player *p, double heat);
    
    //Sets whether the player is currently overheated. Sends OverheatStateChanged
    //callback if different from the current 'overheated state'.
    void (*SetOverheated)(Player *p, int overheated);
    void (*SetOverheatedNoLock)(Player *p, int overheated);
    
    //Modifiers: modules can use these values to affect dissipation, 
    //shutdown time, gun and bomb heat without messing with item properties.
    void (*ModHeatDissipationModifier)(Player *p, double amount);
    void (*ModHeatShutdownTimeModifier)(Player *p, int amount);
    void (*ModGunHeatModifier)(Player *p, double amount);
    void (*ModBombHeatModifier)(Player *p, double amount);
    
    //Whether or not the player is currently "super'd", meaning weapons
    //do not generate heat.
    void (*SetSuper)(Player *p, int on);
} Ihsheat;

#endif
