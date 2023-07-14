# casio-fx702p
Casio FX-702P Information

After buying several FX702Ps that don't work I investigated the possibility of replacing the display
with a different display technology. This is a bus monitoring microcontroller and an LCD. Most of the 
display can be found in the traffic sent to the two display controller. This is then mirrored to the
display. Using this code an LCD or OLED display can replae any malfunctioning LCD. It cannot,
unfortunately, replace a malfunctioning display controller as the display controllers also hold
other RAM which is used by the FX702P. This would also have to be emulated.

After buying an FX702P where the RAM was apparently broken, I made a RAM emulator that replaced the faulty
RAM. This didn't work on my unit, however, as apparently the RAm is fine and the error is in a 
processor (or display controller, I can't pin it down). The RAM emulator did work on another unit 
that I used to prototype the display minitoring code. 

When I added flash storage to the emulation RAM I found that the FX702P seems to store the positions of the programs
or whether they exist in RAM in the processors or display controllers. This means that if a RAM image is 
saved to flash and then re-loaded into a blank FX702P (after CLR ALL, for instance) then the loaded programs
do not appear. The memory contents do appear, however, but just havig those saveable and loadable isn't really
as much use as having the programs able to do that as well.

So, a RAM emulation with flash storage as I have done on the PRO-101 and FX201P doesn't seem to be
possible on the FX702P.


