##
## Hyperspace 1v1 Dueling
## D1st0rt, SSCE Hyperspace
## License: MIT/X11
## Created: 2009-06-05
## Updated: 2009-06-09
##
## 1v1 5-kill, win by 2, no count for double kills
##
import asss

chat = asss.get_interface(asss.I_CHAT)
game = asss.get_interface(asss.I_GAME)
stats = asss.get_interface(asss.I_STATS)
lm = asss.get_interface(asss.I_LOGMAN)
sql = asss.get_interface(asss.I_RELDB)

def dbcb_noop(status, res):
    pass

class HS_Duel:
    def __init__(self, arena):
        self.arena = arena
        self.cbs = []
        self.cmds = []

        self.start_timer = None
        self.dk_timer = None

        self.p1 = None
        self.p2 = None
        self.kills = 5
        self.winby = 2
        self.active = False
        self.ready = []
        self.dk_check = False

    def start(self):
        if self.p1.arena == self.arena and self.p2.arena == self.arena and self.p1.ship != asss.SHIP_SPEC and self.p2.ship != asss.SHIP_SPEC:
            self.active = True
            stats.ScoreReset(self.p1, asss.INTERVAL_RESET)
            stats.ScoreReset(self.p2, asss.INTERVAL_RESET)
            game.LockArena(self.arena, 0, 0, 0, 0)
            #wait 4 pymod spawner
            #game.ShipReset(self.arena)
            chat.SendArenaSoundMessage(self.arena, asss.SOUND_GOAL, "GOGOGO!")
        else:
            chat.SendArenaMessage(self.arena, "Duel canceled, both players must be in ships")
            self.end()

        self.start_timer = None
        return False

    def end(self):
        self.p1 = None
        self.p2 = None
        self.active = False
        del self.ready[:]
        game.UnlockArena(self.arena, 0, 0)

    def cleanup(self):
        del self.cbs[:]
        del self.cmds[:]
        del self.ready[:]
        self.p1 = None
        self.p2 = None
        asss.for_each_player(lambda p : self.del_pdat(p))
        self.arena = None

    def del_pdat(self, p):
        if p.arena == self.arena:
                for attr in ['t', 'id', 'rating']:
                    try:
                        delattr(p, 'hs_duel' + attr)
                    except: pass

    def rating_changes(self, winner, loser):
        delta = winner.hs_duel_rating - loser.hs_duel_rating
        x1 = 1.0 / (1 + pow(10, delta/400.0))
        x2 = 1.0 - x1
        lrc = int(30.0 * (0.0 - x1))
        wrc = int(30.0 * (1.0 - x2))
        return (wrc, lrc)

    def save_forfeit(self, loser, notes):
        if loser == self.p1:
            winner = self.p2
        else:
            winner = self.p1

        winner_kills = stats.GetStat(winner, asss.STAT_KILLS, asss.INTERVAL_RESET)
        loser_kills = stats.GetStat(loser, asss.STAT_KILLS, asss.INTERVAL_RESET)
        self.save_duel(winner, loser, winner_kills, loser_kills, notes)

    def save_duel(self, winner, loser, winner_kills, loser_kills, notes = ' '):
        (wrc, lrc) = self.rating_changes(winner, loser)
        winner.hs_duel_rating += wrc
        loser.hs_duel_rating += lrc
        chat.SendArenaMessage(self.arena, "Rating changes: %s (+%d -> %d)    %s (%d -> %d)" % (
        winner.name, wrc, winner.hs_duel_rating, loser.name, lrc, loser.hs_duel_rating))
        query = "INSERT INTO hs_duels (%s) VALUES (%d, %d, %d, %d, %d, %d, %d, %d, '%s')" % (
            ', '.join(['winner_id', 'loser_id', 'winner_score', 'loser_score',
                'winner_rc', 'loser_rc', 'winner_rating', 'loser_rating', 'notes']),
            winner.hs_duel_id, loser.hs_duel_id, winner_kills, loser_kills, wrc, lrc,
            winner.hs_duel_rating, loser.hs_duel_rating, notes)
        sql.Query(dbcb_noop, 0, query)
        query = 'UPDATE hs_duelers SET rating = %d where id = %d' % (
            winner.hs_duel_rating, winner.hs_duel_id)
        sql.Query(dbcb_noop, 0, query)
        query = 'UPDATE hs_duelers SET rating = %d where id = %d' % (
            loser.hs_duel_rating, loser.hs_duel_id)
        sql.Query(dbcb_noop, 0, query)

    def clear_doublekill(self):
        self.dk_check = False
        self.dk_timer = None

        kills1 = stats.GetStat(self.p1, asss.STAT_KILLS, asss.INTERVAL_RESET)
        kills2 = stats.GetStat(self.p2, asss.STAT_KILLS, asss.INTERVAL_RESET)

        kills = max(kills1, kills2)
        delta = abs(kills1 - kills2)

        if kills >= self.kills and delta >= self.winby:
            if kills1 > kills2:
                killer = self.p1
                killed = self.p2
            else:
                killer = self.p2
                killed = self.p1
            chat.SendArenaMessage(self.arena, "%s Wins!" % killer.name)
            self.save_duel(killer, killed, kills, min(kills1, kills2))
            self.end()

        return False

def dbcb_noop(status, res):
    pass

def load_id(p):
    (ok, name) = sql.EscapeString(p.name)
    if ok == 1:
        query = "SELECT id FROM hs_players WHERE name = '%s'" %  name
        sql.Query(lambda status, res : dbcb_id(status, res, p), 0, query)
    else:
        lm.LogP(asss.L_ERROR, "hs_duel", p, "Error escaping id query")

def dbcb_id(status, res, p):
    if status == 0 and res is not None:
        count = sql.GetRowCount(res)
        if count > 1:
            lm.LogP(asss.L_ERROR, "hs_duel", p, "Multiple rows returned from MySQL: using first.")

        if count == 0:
            lm.LogP(asss.L_ERROR, "hs_duel", p, "Player missing Hyperspace id")
        else:
            row = sql.GetRow(res)
            id = int(sql.GetField(row, 0))
            p.hs_duel_id = id
            load_rating(p)
    else:
            print status
            print res
            lm.LogP(asss.L_ERROR, "hs_duel", p, "Database error in id query")

def load_rating(p):
        query = 'SELECT rating FROM hs_duelers WHERE id = %d' % p.hs_duel_id
        sql.Query(lambda status, res : dbcb_rating(status, res, p), 0, query)

def dbcb_rating(status, res, p):
    if status == 0 and res is not None:
        count = sql.GetRowCount(res)
        if count > 1:
            lm.LogP(asss.L_ERROR, "hs_duel", p, "Multiple rows returned from MySQL: using first.")

        if count == 0:
            p.hs_duel_rating = 1000
            query = 'INSERT INTO hs_duelers (id) VALUES (%d)' % p.hs_duel_id
            sql.Query(dbcb_noop, 0, query)
        else:
            row = sql.GetRow(res)
            rating = int(sql.GetField(row, 0))
            p.hs_duel_rating = rating
    else:
        lm.LogP(asss.L_ERROR, "hs_duel", p, "Database error in rating query")

def c_challenge(cmd, params, p, targ):
    """Module: <py> hs_duel
Targets: player
Args: none
Challenges a player to a duel. Any challenge you make will overwrite a previous challenge."""
    ad = p.arena.hs_duel
    if ad.p1 is None and ad.p2 is None:
        if type(targ) == asss.PlayerType and targ != p:
            p.hs_duel_t = targ
            chat.SendMessage(p, "You challenged %s to a duel" % targ.name)
            chat.SendMessage(targ,
            "%s (R: %d) has challenged you to a duel! PM with ?accept if you accept" % (
                p.name, p.hs_duel_rating))
    else:
        chat.SendMessage(p, "A duel is already in progress in this arena, try another")

def c_accept(cmd, params, p, targ):
    """Module: <py> hs_duel
Targets: player
Args: none
Accepts a challenge from the target player."""
    ad = p.arena.hs_duel
    if ad.p1 is None and ad.p2 is None:
        if type(targ) == asss.PlayerType:
            if targ.hs_duel_t == p:
                ad.p1 = targ
                ad.p2 = p
                targ.hs_duel_t = None
                chat.SendArenaMessage(p.arena,
                "%s (R: %d) has accepted a challenge from %s (R: %d)! Both players need to ?ready" % (
                    ad.p2.name, ad.p2.hs_duel_rating, ad.p1.name, ad.p1.hs_duel_rating))
    else:
        chat.SendMessage(p, "A duel is already in progress in this arena, try another")

def c_ready(cmd, params, p, targ):
    """Module: <py> hs_duel
Targets: none
Args: none
An accepted duel will start when both players are ready."""
    ad = p.arena.hs_duel
    if (p == ad.p1 or p == ad.p2) and p not in ad.ready:
        ad.ready.append(p)
        chat.SendArenaMessage(p.arena, "%s is ready to begin" % p.name)
    else:
        return

    if len(ad.ready) == 2:
        chat.SendArenaMessage(p.arena, "Both players have accepted. Duel beginning in 10 seconds")
        ad.start_timer = asss.set_timer(ad.start, 1000)

def c_rating(cmd, params, p, targ):
    """Module: <py> hs_duel
Targets: player or none
Args: none
Displays the duel rating of target player or yourself."""
    if type(targ) == asss.PlayerType:
        player = targ
    else:
        player = p

    chat.SendMessage(p, "%s (R:%d)" % (player.name, player.hs_duel_rating))

def player_kill(a, killer, killed, bty, flags, pts, green):
    ad = a.hs_duel
    if ad.active:
        kills1 = stats.GetStat(ad.p1, asss.STAT_KILLS, asss.INTERVAL_RESET)
        kills2 = stats.GetStat(ad.p2, asss.STAT_KILLS, asss.INTERVAL_RESET)

        if ad.dk_check:
            chat.SendArenaMessage(a, "Double kill, does not count")
            kills1 -= 1
            kills2 -= 1
            stats.SetStat(ad.p1, asss.STAT_KILLS, asss.INTERVAL_RESET, kills1)
            stats.SetStat(ad.p2, asss.STAT_KILLS, asss.INTERVAL_RESET, kills2)
            chat.SendArenaMessage(a, "%s : %d    %s : %d " % (ad.p1.name, kills1, ad.p2.name, kills2))
        else:
            chat.SendArenaMessage(a, "%s : %d    %s : %d " % (ad.p1.name, kills1, ad.p2.name, kills2))
            ad.dk_check = True
            ad.dk_timer = asss.set_timer(ad.clear_doublekill, 100)

    return (pts, green)

def player_action(p, action, arena):
    if action == asss.PA_ENTERARENA:
        p.hs_duel_t = None
        load_id(p)
    else:
        ad = arena.hs_duel
        if ad.active and p in ad.ready:
            if action == asss.PA_LEAVEARENA:
                chat.SendArenaMessage(arena, "%s forfeits (left arena)" % p.name)
                ad.save_forfeit(p, 'forfeit (left arena)')
                ad.end()
        for attr in ['t', 'id', 'rating']:
            try:
                delattr(p, 'hs_duel' + attr)
            except: pass

def shipfreq_change(p, ship, oldship, freq, oldfreq):
    ad = p.arena.hs_duel
    if ad.active and p in ad.ready:
        if ship == asss.SHIP_SPEC:
            chat.SendArenaMessage(p.arena, "%s forfeits (specced)" % p.name)
            ad.save_forfeit(p, 'forfeit (specced)')
            ad.end()

def mm_attach(a):
    try:
        a.hs_duel = HS_Duel(a)
        a.hs_duel.cmds.append(asss.add_command('challenge', c_challenge, a))
        a.hs_duel.cmds.append(asss.add_command('accept', c_accept, a))
        a.hs_duel.cmds.append(asss.add_command('ready', c_ready, a))
        a.hs_duel.cmds.append(asss.add_command('rating', c_rating, a))
        a.hs_duel.cbs.append(asss.reg_callback(asss.CB_KILL, player_kill, a))
        a.hs_duel.cbs.append(asss.reg_callback(asss.CB_SHIPFREQCHANGE, shipfreq_change, a))
        a.hs_duel.cbs.append(asss.reg_callback(asss.CB_PLAYERACTION, player_action, a))
    except:
        lm.LogA(asss.L_ERROR, 'hs_duel', a, 'Error during attach')
        mm_detach(a)

def mm_detach(a):
    try:
        a.hs_duel.cleanup()
        delattr(a, 'hs_duel')
    except: pass
