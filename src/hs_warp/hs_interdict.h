#ifndef HS_INTERDICT_H
#define HS_INTERDICT_H

#define I_HS_INTERDICT "hs_interdict-1"

typedef struct Ihsinterdict
{
	INTERFACE_HEAD_DECL
	void (*RemoveInterdiction)(Player *p);

} Ihsinterdict;

#endif /* HS_INTERDICT_H */
