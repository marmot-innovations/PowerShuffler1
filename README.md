# PowerShuffler1
Summary: PowerShuffler1 is a HV isolated DC-DC charger PCBA for balancing weak cells within a large battery stack of Li-Ion cells.

Theory of operation: In a battery stack with 99 lithium-ion cells in series, there might be 1 or 2 weak cells that limit the usability of the entire battery pack. Let's assume the 99 li-ion cells are arranged in groups of 9 and there are certain groups that contain cells which are stronger than others. PowerShuffler1 is a PCBA that will piggy-back on top of the existing BMS that comes with the battery pack and draw power from the stronger group in order to charge up the 1 weak cell wherever it sits in the series of 99 cells. PowerShuffler1 has built-in intelligence not to over-discharge the stronger group by measuring the voltage of the 1 weak cell as it rises, and compares it to 1/9th of the stronger group's total voltage as it falls to within a certain threshold. Pre-programmed hysteresis prevents excessive start-stop charging as the cells are put through normal usage during the time when the existing BMS kicks in. Although intrusive, PowerShuffler1 is meant to be a diagnostics tool to verify and characterize weak cells within a battery stack on top of a pre-existing BMS. It does not replace the need for servicing the battery pack or using a better BMS down the road. PowerShuffler1 has no wired or wireless communication, which is not accessible anyway if piggy-backed within a battery pack enclosure.

Included in this project are:
1. Firmware for master MCU
2. Firmware for client MCU
3. PCB design for PCBA

Read the individual README for further details. Pictures will be added later to provide instructions for programming and installation, as well as board modifications where needed. Test data will be provided eventually.
