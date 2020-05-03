PowerShuffler 1.0 PCB design schematic, layout, Gerbers, and BOM
KiCAD 5.0.0 for PCB design
Microsoft Excel web for BOM
No 3D model
No XY pick and place Centroid
No ODB++
No footprint library
No schematic library

Design bugs and comments:
1. S2 pins 3 and 4 are swapped. Use wire wrap to un-swap them.
2. F2 will drop about 0.1V under full 500mA load when charging. No way to measure OCV in this version. Manually take measurements and compensate by adjusting master MCU.
3. R12 cannot be switched off to stop charging. Everytime the client MCU is turned on to take a measurement it will move energy.
4. To program master MCU, remove R6 and C7 first, then replace them once programming is completed. These components will put too much load on ISP TPI programmer.
5. To program client MCU, remove C24 and (optionally) R22 first, then replace them once completed. These components will put too much load on ISP TPI programmer.
6. J2 and J4 do not need soldered headers for programming. Just shove a 5-pin 0.1" straight strip header into it to establish electrical contact. Look up "sneaky footprints PCB" on the web.
7. Mounting holes are for 4-40 screws and stand-offs.
8. Peak 75% efficiency during max charge rate as measured.
9. Less than 0.5mA current draw from input when idle, not charging.
