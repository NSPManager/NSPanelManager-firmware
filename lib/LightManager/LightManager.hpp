#ifndef LIGHTMANAGER_HPP
#define LIGHTMANAGER_HPP

#include <Arduino.h>
// #include <Light.hpp>
class Light;
#include <list>

class LightManager {
public:
  static void ChangeLightsToLevel(std::list<Light *> *lights, uint8_t level);
  static void ChangeLightToColorTemperature(std::list<Light *> *lights, uint16_t kelvin);
  static void ChangeLightsToColorSaturation(std::list<Light *> *lights, uint16_t saturation);
  static void ChangeLightsToColorHue(std::list<Light *> *lights, uint16_t saturation);
  static std::list<Light *> getCeilingLightsThatAreOn();
  static std::list<Light *> getTableLightsThatAreOn();
  static std::list<Light *> getAllCeilingLightsThatAreOn();
  static std::list<Light *> getAllTableLightsThatAreOn();
  static std::list<Light *> getAllLightsThatAreOn();
  static std::list<Light *> getAllCeilingLights();
  static std::list<Light *> getAllTableLights();
  static std::list<Light *> getAllLights();
  static Light *getLightById(uint16_t id);
  static bool anyCeilingLightsOn();
  static bool anyTableLightsOn();
  static bool anyLightsOn();

private:
  static inline std::list<Light *> _levelUpdates;
  static inline std::list<Light *> _temperatureUpdates;
  static inline std::list<Light *> _rgbUpdates;
};

#endif
