/*
 * hardware_config.h
 *
 *  Created on: Apr 30, 2026
 *      Author: will
 */

#ifndef SRC_HARDWARE_CONFIG_H_
#define SRC_HARDWARE_CONFIG_H_

//Supply voltage divider
#define SUPPLY_DIVIDER_R_HIGH_OHMS	47000.0f
#define SUPPLY_DIVIDER_R_LOW_OHMS	5000.0f

//Fuel sender voltage divider
#define FUEL_DIVIDER_R_HIGH_OHMS	10000.0f //3500
#define FUEL_DIVIDER_R_LOW_OHMS		5000.0f //3357

//Resistance of fuel guage TODO: Double check ;)
#define FUEL_GUAGE_R_OHMS			330.0f
#define VS_COMP						0.5f

//Oil pressure voltage divider
//Low side resistor is the transmitter
#define OIL_DIVIDER_R_HIGH_OHMS		100.0f

//Coolant temperature voltage divider
//Low side resistor is the sensor
#define COOLANT_DIVIDER_R_HIGH_OHMS	10000.0f

#endif /* SRC_HARDWARE_CONFIG_H_ */
