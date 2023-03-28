/*
 * pages.h
 *
 *  Created on: Mar 6, 2023
 *      Author: Tim Panajott
 */

#ifndef LIB_INTERFACEMANAGER_PAGES_H_
#define LIB_INTERFACEMANAGER_PAGES_H_

#include <Arduino.h>
#include <InterfaceManager.h>

class HomePage {
public:
	static void setDimmingValue(uint8_t value);
	static int getDimmingValue();
	static void setColorTempValue(uint8_t value);
	static int getColorTempValue();
	static void setCeilingBrightnessLabelText(uint8_t value);
	static void setTableBrightnessLabelText(uint8_t value);
	static void setCeilingLightsState(bool state);
	static void setTableLightsState(bool state);
	static void setSliderLightLevelColor(uint color);
	static void setSliderColorTempColor(uint color);
	static void setHighlightCeilingVisibility(bool visable);
	static void setHighlightTableVisibility(bool visable);
};


#endif /* LIB_INTERFACEMANAGER_PAGES_H_ */