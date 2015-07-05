# dist: public

import math
import asss
import random

MAXFLAGS = 255

flagcore = asss.get_interface(asss.I_FLAGCORE)
mapdata = asss.get_interface(asss.I_MAPDATA)
cfg = asss.get_interface(asss.I_CONFIG)
lm = asss.get_interface(asss.I_LOGMAN)
prng = asss.get_interface(asss.I_PRNG)
game = asss.get_interface(asss.I_GAME)
chat = asss.get_interface(asss.I_CHAT)


# utils

def clip(v, bottom, top):
  if v < bottom:
    return bottom
  elif v > top:
    return top
  else:
    return v

def wrap(v, bottom = 0, top = 1023):
  while 1:
    if v < bottom:
      v = 2 * bottom - v
    elif v > top:
      v = 2 * top - v
    else:
      return v

def circular_random(cx, cy, radius, radiusmin):
  r = prng.Uniform() * (float(radius) - float(radiusmin))

  cx = float(cx) + 0.5
  cy = float(cy) + 0.5
  theta = prng.Uniform() * 2 * math.pi
  return wrap(int(cx + (r + radiusmin) * math.cos(theta))), \
         wrap(int(cy + (r + radiusmin) * math.sin(theta)))


# flag game

class WZSets:
  def __init__(me, a):
    me.load(a)

  def load(me, a):
    c = a.cfg

    # cfghelp: Flag:ResetDelay, arena, int, def: 0
    # The length of the delay between flag games.
    me.resetdelay = cfg.GetInt(c, "Flag", "ResetDelay", 0)
    # cfghelp: Flag:SpawnX, arena, int, def: 512
    # The X coordinate that new flags spawn at (in tiles).
    me.spawnx = cfg.GetInt(c, "Flag", "SpawnX", 512)
    # cfghelp: Flag:SpawnY, arena, int, def: 512
    # The Y coordinate that new flags spawn at (in tiles).
    me.spawny = cfg.GetInt(c, "Flag", "SpawnY", 512)
    # cfghelp: Flag:SpawnRadius, arena, int, def: 50
    # How far from the spawn center that new flags spawn (in
    # tiles).
    me.spawnr = cfg.GetInt(c, "Flag", "SpawnRadius", 50)
    # cfghelp: Flag:SpawnRadiusMin, arena, int, def: 0
    # How far from the spawn center minimally new flags spawn (in tiles)
    me.spawnrmin = cfg.GetInt(c, "Flag", "SpawnRadiusMin", 0)
    # cfghelp: Flag:DropRadius, arena, int, def: 2
    # How far from a player do dropped flags appear (in tiles).
    me.dropr = cfg.GetInt(c, "Flag", "DropRadius", 2)
    # cfghelp: Flag:FriendlyTransfer , arena, bool, def: 0
    # Whether you get a teammates flags when you kill him.
    me.friendlytransfer = cfg.GetInt(c, "Flag", "FriendlyTransfer", 0)
    # cfghelp for this one is in clientset.def
    me.carryflags = cfg.GetInt(c, "Flag", "CarryFlags", asss.CARRY_ALL)

    # cfghelp: Flag:DropOwned, arena, bool, def: 1
    # Whether flags you drop are owned by your team.
    me.dropowned = cfg.GetInt(c, "Flag", "DropOwned", 1)
    # cfghelp: Flag:DropCenter, arena, bool, def: 0
    # Whether flags dropped normally go in the center of the map, as
    # opposed to near the player.
    me.dropcenter = cfg.GetInt(c, "Flag", "DropCenter", 0)

    # cfghelp: Flag:NeutOwned, arena, bool, def: 0
    # Whether flags you neut-drop are owned by your team.
    me.neutowned = cfg.GetInt(c, "Flag", "NeutOwned", 0)
    # cfghelp: Flag:NeutCenter, arena, bool, def: 0
    # Whether flags that are neut-droped go in the center, as
    # opposed to near the player who dropped them.
    me.neutcenter = cfg.GetInt(c, "Flag", "NeutCenter", 0)

    # cfghelp: Flag:TKOwned, arena, bool, def: 1
    # Whether flags dropped by a team-kill are owned by your team,
    # as opposed to neutral.
    me.tkowned = cfg.GetInt(c, "Flag", "TKOwned", 1)
    # cfghelp: Flag:TKCenter, arena, bool, def: 0
    # Whether flags dropped by a team-kill spawn in the center, as
    # opposed to near the killed player.
    me.tkcenter = cfg.GetInt(c, "Flag", "TKCenter", 0)

    # cfghelp: Flag:SafeOwned, arena, bool, def: 1
    # Whether flags dropped from a safe zone are owned by your
    # team, as opposed to neutral.
    me.safeowned = cfg.GetInt(c, "Flag", "SafeOwned", 1)
    # cfghelp: Flag:SafeCenter, arena, bool, def: 0
    # Whether flags dropped from a safe zone spawn in the center,
    # as opposed to near the safe zone player.
    me.safecenter = cfg.GetInt(c, "Flag", "SafeCenter", 0)

    # cfghelp: Flag:WinDelay, arena, int, def: 200
    # The delay between dropping the last flag and winning (ticks).
    me.windelay = cfg.GetInt(c, "Flag", "WinDelay", 200)

    # cfghelp: Flag:CantCarryOwned, arena, bool, def: 1
    # Whether flags that aren't able to be carried when you kill someone
    # holding flags are owned or neuted.
    me.cantcarryowned = cfg.GetInt(c, "Flag", "CantCarryOwned", 1)

    # cfghelp: Flag:UseDropRegions, arena, bool, def: 0
    # Whether to restrict flag drops to a certain areas of the map
    # defined by Flag:DropAllowRegion.
    me.usedropregion = cfg.GetInt(c, "Flag", "UseDropAllowRegion", 0)

    # cfghelp: Flag:DisallowedDropToCenter, arena, bool, def: 1
    # If flags that are dropped outside of the allowed drop area become
    # neuted in the center area instead.
    me.disalloweddropcenter = cfg.GetInt(c, "Flag", "DisallowedDropToCenter", 1)

    # cfghelp: Flag:DropAllowRegion, arena, string
    # Where flags are allowed to be owned
    me.dropallowrgnname = cfg.GetStr(c, "Flag", "DropAllowRegion")

    # cfghelp: Flag:WarpPlayersOnWin, arena, bool, def: 0
    # Whether to warp the freqs specified by Flag:MaxFreqToWarp
    # when the flag game is won.
    me.warpplayersonwin = cfg.GetInt(c, "Flag", "WarpPlayersOnWin", 0)

    # cfghelp: Flag:MaxFreqToWarp, arena, int, def: 1
    # When Flag:WarpPlayersOnWin is set, this specifies
    # the highest frequency that will be warped.
    me.maxfreqtowarp = cfg.GetInt(c, "Flag", "MaxFreqToWarp", 1)

    # cfghelp: Flag:ConsecutiveWinsToShuffle, arena, int, def: 1
    # If greater than 0, how many wins hs_fg_wz will wait
    # before shuffling the freqs equal or less than to Flag:MaxFreqToShuffle
    me.consecutivewinstoshuffle = cfg.GetInt(c, "Flag", "ConsecutiveWinsToShuffle", 1)

    # cfghelp: Flag:MaxFreqToShuffle, arena, int, def: 1
    # When Flag:ConsecutiveWinsToShuffle is set, this specifies
    # the highest frequency that will be shuffled.
    me.maxfreqtoshuffle = cfg.GetInt(c, "Flag", "MaxFreqToShuffle", 1)

    # cfghelp: Flag:MinFreqToShuffle, arena, int, def: 1
    # When Flag:ConsecutiveWinsToShuffle is set, this specifies
    # the lowest frequency that will be shuffled.
    me.minfreqtoshuffle = cfg.GetInt(c, "Flag", "MinFreqToShuffle", 1)

    # cfghelp: Flag:DisallowFlagTeamStart, arena, int, def: 1
    # Sets the starting frequency of teams that are not
    # permitted to pick up flags.
    me.disallowflagteamstart = cfg.GetInt(c, "Flag", "DisallowFlagTeamStart", 0)

    # cfghelp: Flag:DisallowFlagTeamEnd, arena, int, def: 90
    # Sets the ending frequency of teams that are not
    # permitted to pick up flags.
    me.disallowflagteamend = cfg.GetInt(c, "Flag", "DisallowFlagTeamEnd", 90)

    me.ptrdropallowrgn = None
    if me.usedropregion == 1:
      me.ptrdropallowrgn = mapdata.FindRegionByName(a, me.dropallowrgnname)
      if not me.ptrdropallowrgn:
        lm.LogA(asss.L_WARN, 'hs_fg_wz', a, ("specified drop allow region was not found on the current map (%s)")% (me.dropallowrgnname))
        me.usedropregion = 0

    # cfghelp: Flag:FlagCount, arena, rng, range: 0-255, def: 0
    # How many flags are present in this arena.
    count = cfg.GetStr(c, "Flag", "FlagCount")
    try:
      me.min, me.max = map(int, count.split('-'))
    except:
      try:
        me.min = me.max = int(count)
      except:
        me.min = me.max = 0

    me.max = clip(me.max, 0, MAXFLAGS)
    me.min = clip(me.min, 0, me.max)

    me.allowpriv = cfg.GetInt(c, "Hyperspace", "PrivFlag", 1)

    # Hyperspace flag team junk
    me.privfreqstart = cfg.GetInt(c, "Team", "PrivFreqStart", 100)
    me.privfreqmax = cfg.GetInt(c, "Team", "MaxFrequency", (me.privfreqstart + 2))

    # cfghelp: Flag:MinPlayersPerTeam, arena, int, def: 2
    # Sets the minimum number of players required on each team (specified by Team:PrivFreqStart and
    # Team:MaxFrequency) to allow flags to be owned. If set and not met, all flags will be placed
    # as neutral flags.
    me.minplayersperteam = cfg.GetInt(c, "Flag", "MinPlayersPerTeam", 2)


# useful flag things
def check_flag_team_sizes(arena):
  freqs = {}
  result = True

  def get_freq_counts(player):
    if player.type != asss.T_CONT and player.type != asss.T_VIE:
      return

    if player.arena == arena and player.ship != asss.SHIP_SPEC:
      if player.freq in freqs:
        freqs[player.freq] += 1
      else:
        freqs[player.freq] = 1

  if arena.fg_wz_sets.minplayersperteam > 0:
    # get freq player counts
    asss.for_each_player(get_freq_counts)

    for i in range(arena.fg_wz_sets.privfreqstart, arena.fg_wz_sets.privfreqmax):
      if i not in freqs or freqs[i] < arena.fg_wz_sets.minplayersperteam:
        result = False
        break

  return result;



def neut_flag(a, i, x, y, r, rmin, freq):
  # record it as a neut so we can behave differently when spawning it
  a.fg_wz_neuts.append(i)
  # move it to none, but record x, y, freq
  f = asss.flaginfo()
  f.state = asss.FI_NONE
  f.x, f.y = x, y
  f.freq = freq
  f.carrier = None
  flagcore.SetFlags(a, i, f)


def spawn_flag(a, i, cx, cy, r, rmin, freq, fallback = 1):
  good = 0
  tries = 0
  while not good and tries < 30:
    # assume this will work
    good = 1
    # pick a random point
    x, y = circular_random(cx, cy, r + tries, rmin + tries)
    # and move off of any tiles
    x, y = mapdata.FindEmptyTileNear(a, x, y)
    # check if it's hitting another flag:
    for j in range(a.fg_wz_current):
      if i != j:
        n2, f2 = flagcore.GetFlags(a, j)
        if n2 == 1 and f2.state == asss.FI_ONMAP and \
           f2.x == x and f2.y == y:
          good = 0
    # and check for no-flag regions:
    if good:
      foundnoflags = []
      def check_region(r):
        if mapdata.RegionChunk(r, asss.RCT_NOFLAGS) is not None:
          foundnoflags.append(1)
      mapdata.EnumContaining(a, x, y, check_region)
      if foundnoflags:
        good = 0
    # if we did hit another flag, bump up the radius so we don't get
    # stuck here.
    tries += 1

  if good:
    f = asss.flaginfo()
    f.state = asss.FI_ONMAP
    f.x, f.y = x, y
    f.freq = freq
    f.carrier = None
    flagcore.SetFlags(a, i, f)
  elif fallback:
    # we have one fallback option: centering it
    lm.LogA(asss.L_WARN, 'flagcore', a,
        ("failed to place a flag at (%d,%d)-%d, "
         "falling back to center") % (cx, cy, r))
    sets = a.fg_wz_sets
    spawn_flag(a, i, sets.spawnx, sets.spawny, sets.spawnr, sets.spawnrmin, freq, 0)
  else:
    # couldn't place in original location, and couldn't place in
    # center either. this is really bad.
    lm.LogA(asss.L_ERROR, 'flagcore', a,
        ("failed to place a flag at (%d,%d)-%d, "
         "fallback disabled")% (cx, cy, r))


# the spawn timer

def spawn_timer(a):
  current = a.fg_wz_current
  sets = a.fg_wz_sets
  neuts = a.fg_wz_neuts

  # pick a new flag count?
  if current < sets.min:
    a.fg_wz_current = current = prng.Number(sets.min, sets.max)

  # spawn flags?
  for i in range(current):
    n, f = flagcore.GetFlags(a, i)
    if n != 1: continue
    if f.state == asss.FI_NONE:
      # handle neuted flags specially. their x, y, freq will have
      # been set correctly when they were neuted (by cleanup).
      if i in neuts:
        neuts.remove(i)
        x, y = f.x, f.y
        if sets.neutcenter:
          r = sets.spawnr
          r = sets.spawnrmin
        else:
          r = sets.dropr
          rmin = 0
        freq = f.freq
      else:
        x = sets.spawnx
        y = sets.spawny
        r = sets.spawnr
        rmin = sets.spawnrmin
        freq = -1

      spawn_flag(a, i, x, y, r, rmin, freq)


# winning

def check_win(a):
  current = a.fg_wz_current
  ownedflags = 0
  winfreq = -1

  for i in range(current):
    n, f = flagcore.GetFlags(a, i)
    assert n == 1
    if f.state == asss.FI_ONMAP:
      if f.freq == winfreq:
        ownedflags += 1
      else:
        winfreq = f.freq
        ownedflags = 1

  # just to check
  for i in range(current, a.fg_wz_sets.max):
    n, f = flagcore.GetFlags(a, current)
    assert n == 0 or f.state == asss.FI_NONE

  if ownedflags == current and winfreq != -1:
    # we have a winner!
    points = asss.call_callback(asss.CB_WARZONEWIN, (a, winfreq, 0), a)
    # hardcoded default
    if points == 0: points = 1000
    flagcore.FlagReset(a, winfreq, points)
    a.fg_wz_current = 0
    if a.fg_wz_sets.warpplayersonwin:
      for i in range(0, a.fg_wz_sets.maxfreqtowarp+1):
        game.GivePrize((a, i), asss.PRIZE_WARP, 1)

    if a.fg_wz_last_freq_to_win != winfreq:
      a.fg_wz_last_freq_to_win = winfreq
      a.fg_wz_consecutive_wins = 0

    a.fg_wz_consecutive_wins += 1
    if winfreq <= a.fg_wz_sets.maxfreqtoshuffle and \
       winfreq >= a.fg_wz_sets.minfreqtoshuffle:
      playerlist = []
      if a.fg_wz_sets.consecutivewinstoshuffle > 0 and \
         a.fg_wz_consecutive_wins >= a.fg_wz_sets.consecutivewinstoshuffle:
        a.fg_wz_last_freq_to_win = -1
        a.fg_wz_consecutive_wins = 0
        chat.SendArenaMessage(a, "---SPECCING EVERYONE TO EVEN TEAMS---")
        def playerstolist(p):
          if p.freq <= a.fg_wz_sets.maxfreqtoshuffle and \
             p.freq >= a.fg_wz_sets.minfreqtoshuffle and \
             p.arena == a and p.type != asss.T_FAKE:
            playerlist.append((p, p.ship))
            game.SetShipAndFreq(p, asss.SHIP_SPEC, a.specfreq)
        asss.for_each_player(playerstolist)
        random.shuffle(playerlist)

        for (p, ship) in playerlist:
          (actualship, actualfreq) = a.fg_wz_freqman.ShipChange(p, ship, 0)
          game.SetShipAndFreq(p, actualship, actualfreq)





def schedule_win_check(a):
  def check_win_tmr():
    check_win(a)
    del a.fg_wz_checkwintmr
  # note that this releases the old reference, if one existed, which
  # will cancel any previously-set timer, so there is guaranteed to be
  # no win checks before windelay ticks after calling this function.
  a.fg_wz_checkwintmr = asss.set_timer(check_win_tmr, a.fg_wz_sets.windelay)


def flagonmap(a, fid, x, y, freq):
  # any time a flag lands on the map, we should check for a win
  schedule_win_check(a)

def num_flags(p):
  num = 0
  current = p.arena.fg_wz_current
  for i in range(current):
    n, f = flagcore.GetFlags(p.arena, i)
    if n != 1:
      continue
    if f.carrier == p:
      num += 1
  return num

def onkill(a, killer, killed, bty, flags, pts, green):
  # Update can_win on player kill
  if num_flags(killer) > 0:
    a.can_win = check_flag_team_sizes(a)

  return (pts, green)

def is_winning(a, freq):
  winning = True
  current = a.fg_wz_current
  for i in range(current):
    n, f = flagcore.GetFlags(a, i)
    if n != 1:
      continue
    if f.freq != freq:
      winning = False
      break
  return winning

# the flaggame interface
class WZFlagGame:
  iid = asss.I_FLAGGAME

  def Init(me, a):
    # get settings
    a.fg_wz_sets = sets = WZSets(a)
    a.fg_wz_last_freq_to_win = -1
    a.fg_wz_consecutive_wins = 0

    if sets.carryflags < asss.CARRY_ALL:
      lm.LogA(asss.L_ERROR, 'fg_wz', a, 'invalid Flag:CarryFlags for warzone-style game')
      del a.fg_wz_sets
      return

    # set up flag game
    flagcore.SetCarryMode(a, sets.carryflags)
    flagcore.ReserveFlags(a, sets.max)

    # set up more stuff
    a.fg_wz_neuts = []
    a.fg_wz_current = 0

    a.fg_wz_spawntmr = asss.set_timer(lambda: spawn_timer(a), 500)

    a.can_win = False

  def FlagTouch(me, a, p, fid):
    sets = a.fg_wz_sets
    if sets.carryflags == asss.CARRY_ALL:
      cancarry = MAXFLAGS
    else:
      cancarry = sets.carryflags - 1

    if p.flagscarried >= cancarry:
      return

    if sets.allowpriv == 0 and p.freq >= 100:
      return

    if p.freq >= sets.disallowflagteamstart and p.freq < sets.disallowflagteamend:
      return

    # Lock in ability to win when flag is picked up
    if not check_flag_team_sizes(a):
      chat.SendMessage(p, "Note: Flag game cannot be won until every team has at least %d player(s)." % sets.minplayersperteam)
      a.can_win = False
    else:
      a.can_win = True

    # assign him the flag
    f = asss.flaginfo()
    f.state = asss.FI_CARRIED
    f.carrier = p
    flagcore.SetFlags(a, fid, f)


  def Cleanup(me, a, fid, reason, carrier, freq):
    sets = a.fg_wz_sets

    def spawn(owned, center, func=spawn_flag):
      if owned:
        myfreq = freq
      else:
        myfreq = -1

      sizes_okay = check_flag_team_sizes(a)

      # Lock in if they can win based on last flag change
      if reason != asss.CLEANUP_DROPPED:
        a.can_win = sizes_okay

      if is_winning(a, freq):
        # Check if there is enough people on the flagging freqs to win
        if not sizes_okay:
          # Check if they can win based on last flag change
          if not (owned and a.can_win):
            myfreq = -1


      if center:
        x = sets.spawnx
        y = sets.spawny
        r = sets.spawnr
        rmin = sets.spawnrmin
      else:
        x = carrier.position[0] >> 4
        y = carrier.position[1] >> 4
        r = sets.dropr
        rmin = 0
      func(a, fid, x, y, r, rmin, myfreq)

    if reason == asss.CLEANUP_DROPPED:
      if not sets.usedropregion:
        # this acts like a normal drop
        spawn(sets.dropowned, sets.dropcenter)
      else:
        # well now you've done it, we've got to check if this was in an allowed drop region
        if mapdata.Contains(sets.ptrdropallowrgn, carrier.position[0] >> 4, carrier.position[1] >> 4):
          spawn(sets.dropowned, sets.dropcenter)
        else:
          spawn(0, sets.disalloweddropcenter)

    elif reason == asss.CLEANUP_KILL_CANTCARRY:
      if sets.cantcarryowned == 1:
        spawn(sets.dropowned, sets.dropcenter)
      else:
        spawn(0, sets.neutcenter)

    elif reason == asss.CLEANUP_INSAFE:
      # similar, but use safe zone settings
      spawn(sets.safeowned, sets.safecenter)

    elif reason == asss.CLEANUP_KILL_NORMAL:
      # neut flags private freqs get from public freqs through kills
      (ok,f) = flagcore.GetFlags(a, fid)
      if sets.allowpriv == 0 and ok == 1 and f.carrier.freq >= 100:
        spawn(False, False)
      if ok == 1 and f.carrier.freq >= sets.disallowflagteamstart and f.carrier.freq < sets.disallowflagteamend:
        spawn(False, False)

    elif reason == asss.CLEANUP_KILL_TK:
      # only spawn if we're not transferring
      if not sets.friendlytransfer:
        spawn(sets.tkowned, sets.tkcenter)

    elif reason == asss.CLEANUP_SHIPCHANGE or \
       reason == asss.CLEANUP_FREQCHANGE or \
       reason == asss.CLEANUP_LEFTARENA or \
       reason == asss.CLEANUP_KILL_FAKE or \
       reason == asss.CLEANUP_OTHER:
      # neuts
      spawn(sets.neutowned, sets.neutcenter, neut_flag)


# attaching/detaching

def mm_attach(a):
  try:
    a.fg_wz_intref = asss.reg_interface(WZFlagGame(), a)
    a.fg_wz_cbref1 = asss.reg_callback(asss.CB_FLAGONMAP, flagonmap, a)
    a.fg_wz_cbkill1 = asss.reg_callback(asss.CB_KILL, onkill, a)
    a.fg_wz_freqman = asss.get_interface(asss.I_FREQMAN, a)
  except:
    mm_detach(a)

def mm_detach(a):
  for attr in ['intref', 'cbref1', 'current', 'sets', 'neuts', 'checkwintmr', 'spawntmr', 'cbref2', 'freqman', 'cbkill1']:
    try: delattr(a, 'fg_wz_' + attr)
    except: pass