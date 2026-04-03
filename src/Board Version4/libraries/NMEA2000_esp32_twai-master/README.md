# Remote for Raymarine Evolution Autopilots

<table>
<tbody>
  <tr>
    <td rowspan="2" valign="top"><img src="https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Photos/photo1.jpg" alt="photo1" width="500"/></td>
    <td valign="top"><img src="https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Photos/photo5.jpg" alt="photo5" height="200"/></td>
    <td valign="bottom"><img src="https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Photos/photo2.jpg" alt="photo2" width="200"/></td>
  </tr>
  <tr>
    <td valign="top"><img src="https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Photos/photo4.jpg" alt="photo4" height="200"/></td>
    <td valign="top"><img src="https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Photos/photo3.jpg" alt="photo3" height="200"/></td>
  </tr>
</tbody>
</table>



## Libraries used:
* https://github.com/ttlappalainen/NMEA2000
* https://github.com/ttlappalainen/NMEA2000_mcp
* https://github.com/Seeed-Studio/CAN_BUS_Shield
* https://github.com/rogerclarkmelbourne/Arduino_STM32


## Hardware:

### Receiver
* Maple Mini (STM32F103) / Arduino_STM32
* MCP2551 CAN-BUS Module
* Logic Level Converter (https://www.sparkfun.com/products/12009)
* QIACHIP RX480E 433Mhz 4CH RF Receiver (https://qiachip.com/collections/all-products-1/products/qiachip-tx118sa-4-rx480e-kit-433-92mhz-wireless-wide-voltage-coding-transmitter-module-and-decoding-receiver-module-kit-4ch-rf-ev1527-encoding-learning-module-diy-receiver-kit)
* 12V to 5V step down converter

### Remote / Sender
* Qiachip TX118SA 433Mhz 4CH RF Transmitter (https://qiachip.com/collections/all-products-1/products/qiachip-tx118sa-4-rx480e-kit-433-92mhz-wireless-wide-voltage-coding-transmitter-module-and-decoding-receiver-module-kit-4ch-rf-ev1527-encoding-learning-module-diy-receiver-kit)
* Qi wireless charging receiver coil (https://www.aliexpress.com/item/32966646423.html)
* Lipo charger module
* Lipo battery 3.7V
* (Any Qi compatible charger)


### 3D print
keypad and seal were printed using TPE. The front and back were laser cut of acrylic (thanks to [@jliebers](https://github.com/jliebers)!). The remaining parts were printed using PETG. The front was glued to the frame using T7000

![3d model](https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/3D-Models/1_remote_3d_model.png)


## Remote shortcuts

| Keys           | Press and hold for one seconds | Description                 |
|----------------|--------------------------------|-----------------------------|
| +1 & +10       | 1                              | Tack starboard              |
| -1 & -10       | 1                              | Tack portside               |
| +1 & Wind      | 1                              | Key lock                    |
| Standby & Auto | 2                              | Set target wind angle to 0° |


## Commands via tty
| input | Description |
| ----- | ----------- |
| s | Standby |
| a | Auto (course) mode |
| w | Wind Vane mode |
| t | Track mode |
| + | Plus 10 degrees |
| - | Minus 10 degrees |
| c | Confirm Waypoint advance |
| 1 | Key +1 |
| 2 | Key +10 |
| 3 | Key -1 |
| 4 | Key -10 |
| 5 | Keys -1 and -10 pressed simultaneously |
| 6 | Keys +1 and +10 pressed simultaneously |

## Board

![board receiver](https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Board/Autopilot_remote_receiver_brd.png)
![board sender](https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Board/Autopilot_remote_sender_brd.png)

## Schematic

![schematic receiver](https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Schematic/Autopilot_remote_receiver_sch.png)
![schematic sender](https://raw.githubusercontent.com/matztam/raymarine-evo-pilot-remote/master/Schematic/Autopilot_remote_sender_sch.png)
