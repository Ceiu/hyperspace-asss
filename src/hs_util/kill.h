#ifndef KILL_H
#define KILL_H

typedef struct Killer Killer;

#define I_KILL "kill-5"

typedef struct Ikill
{
	INTERFACE_HEAD_DECL

	/** Creates a killer with the specified name. Don't forget to free the
	 * killer using UnloadKiller when finished with it.
	 *
	 * This will attempt to reuse existing killers in the arena, so don't
	 * worry about reusing the same name over several modules.
	 *
	 * @param name the name of the killer to use
	 * @param arena the arena to hold the new killer
	 * @param ship the ship to use for the killer. 0 = warbird.
	 * @param freq the frequency to use for the killer.
	 * @return a Killer object to use with other kill functions
	 */
	Killer * (*LoadKiller)(const char *name, Arena *arena, int ship,
		int freq);

	/** Frees a killer and removes it from the arena.
	 *
	 * NOTE: The killer is only removed if no other modules are using it,
	 * so multiple modules using the same killer are not an issue.
	 *
	 * @param killer the killer to unload
	 */
	void (*UnloadKiller)(Killer *killer);

	/** Kills a player using the specified killer.
	 *
	 * NOTE: the player p *always* receives a L4 thor, regardless of what
	 * the rest of the arena will see.
	 *
	 * @param p the player to kill
	 * @param killer the Killer to use
	 * @param blast the level of thor blast to use. see note.
	 *		0 = only player p will receive the blast.
	 *		1-4 = L1 to L4. Note that an L1 thor is a normal thor
	 * @param only_team if the blast should only hurt teammates
	 */
	void (*Kill)(Player *p, Killer *killer, int blast, int only_team);

	/** Kills a player using another player.
	 *
	 * NOTE: the player p *always* receives a L4 thor, regardless of what
	 * the rest of the arena will see.
	 *
	 * NOTE: The killer will never see the blast.
	 *
	 * @param p the player to kill
	 * @param killer the player to kill p
	 * @param blast the level of thor blast to use. see first note.
	 *		0 = only player p will receive the blast.
	 *		1-4 = L1 to L4. Note that an L1 thor is a normal thor
	 * @param only_team if the blast should only hurt teammates
	 */
	void (*KillWithPlayer)(Player *p, Player *killer, int blast,
		int only_team);

	/** Gets the fake player pointer associated with the killer. This
	 * may be useful to modules that want to send their weapon packets
	 * directly.
	 *
	 * NOTE: this may return NULL. If so, the arena is probably undergoing
	 * a recycle and you shouldn't be using it anyhow. Just watch out.
	 *
	 * NOTE: the player pointer may change over the life of a Killer
	 * object's life. So don't store it for longer than the life of the
	 * calling function.
	 *
	 * @param killer the Killer object
	 * @return The pointer to the fake player representing the killer
	 */
	Player * (*GetKillerPlayer)(Killer *killer);
} Ikill;

#endif /* KILL_H */
