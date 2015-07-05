##
## Hyperspace Owned Ships Frequency Manager
## D1st0rt, SSCE Hyperspace
## License: MIT/X11
## Last Updated 2008-12-28
##
## Sets everyone to spec upon entering, and prevents them from getting
## into ships they do not own. Technically this is achieved by checking
## to see if they have any items on their ship so if they only own the
## hull they will not be able to enter.
##
import asss

chat = asss.get_interface(asss.I_CHAT)
cfg = asss.get_interface(asss.I_CONFIG)
items = asss.get_interface(asss.I_HSCORE_ITEMS)

shipNames = {
	0:'Warbird',
	1:'Javelin',
	2:'Spider',
	3:'Leviathan',
	4:'Terrier',
	5:'Weasel',
	6:'Lancaster',
	7:'Shark',
	8:'Spectator' 
}	

class HS_OwnedShip:
	iid = asss.I_FREQMAN
	
	def InitialFreq(self, p, ship, freq):		
		return asss.SHIP_SPEC, p.arena.specfreq

	def ShipChange(self, p, ship, freq):		
		if ship == asss.SHIP_SPEC or items.hasItemsLeftOnShip(p, ship) > 0:
			return p.arena.fhsos_oldfm.ShipChange(p, ship, freq)		
		else:
			chat.SendMessage(p, "You do not own a %s hull. Please use \"?buy ships\" to examine the ship hulls for sale." % shipNames[ship]);
			return p.ship, p.freq

	def FreqChange(self, p, ship, freq):
		return p.arena.fhsos_oldfm.FreqChange(p, ship, freq)
		

def mm_attach(a):	
	a.fhsos_oldfm = asss.get_interface(asss.I_FREQMAN, a)
	a.fhsos_myint = asss.reg_interface(HS_OwnedShip(), a)

def mm_detach(a):
	for attr in ['myint', 'oldfm']:
		try: delattr(a, 'fhsos_' + attr)
		except: pass
