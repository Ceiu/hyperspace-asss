

#ifndef HSCORE_BUYSELL_H
#define HSCORE_BUYSELL_H

#define A_HSCORE_BUYSELL "hscore_buysell-1"
typedef struct Ahscorebuysell
{
	ADVISER_HEAD_DECL
	
	/*
	These are called after regular checks. Return 1 to
	allow the player to buy or sell, return 0 to deny.
	Note that in order for it to 'look right' for the
	buying or selling player, you should message them
	the reason they can't buy or sell. 
	*/
	
	int (*CanBuy)(Player *p, Item *item, int count, int ship);
	int (*CanSell)(Player *p, Item *item, int count, int ship);
} Ahscorebuysell;

#endif //HSCORE_BUYSELL_H
